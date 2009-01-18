/*
 * Copyright (C) 2005 - 2008 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */
#ifndef __hwlib_h__
#define __hwlib_h__

#include <linux/module.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "regmap.h"		/* srcgen array map output */

#include "asyncmesg.h"
#include "fwcmd_opcodes.h"
#include "post_codes.h"
#include "fwcmd_mcc.h"

#include "fwcmd_types_bmap.h"
#include "fwcmd_common_bmap.h"
#include "fwcmd_eth_bmap.h"
#include "bestatus.h"
/*
 *
 * Macros for reading/writing a protection domain or CSR registers
 * in BladeEngine.
 */
#define PD_READ(fo, field)	ioread32((fo)->db_va + \
		offsetof(struct BE_PROTECTION_DOMAIN_DBMAP_AMAP, field)/8)

#define PD_WRITE(fo, field, val) iowrite32(val, (fo)->db_va + \
		offsetof(struct BE_PROTECTION_DOMAIN_DBMAP_AMAP, field)/8)

#define CSR_READ(fo, field)	ioread32((fo)->csr_va + \
		offsetof(struct BE_BLADE_ENGINE_CSRMAP_AMAP, field)/8)

#define CSR_WRITE(fo, field, val)	iowrite32(val, (fo)->csr_va + \
		offsetof(struct BE_BLADE_ENGINE_CSRMAP_AMAP, field)/8)

#define PCICFG0_READ(fo, field)	ioread32((fo)->pci_va + \
		offsetof(struct BE_PCICFG0_CSRMAP_AMAP, field)/8)

#define PCICFG0_WRITE(fo, field, val)	iowrite32(val, (fo)->pci_va + \
		offsetof(struct BE_PCICFG0_CSRMAP_AMAP, field)/8)

#define PCICFG1_READ(fo, field)	ioread32((fo)->pci_va + \
		offsetof(struct BE_PCICFG1_CSRMAP_AMAP, field)/8)

#define PCICFG1_WRITE(fo, field, val)	iowrite32(val, (fo)->pci_va + \
		offsetof(struct BE_PCICFG1_CSRMAP_AMAP, field)/8)

#ifdef BE_DEBUG
#define ASSERT(c)       BUG_ON(!(c));
#else
#define ASSERT(c)
#endif

/* debug levels */
enum BE_DEBUG_LEVELS {
	DL_ALWAYS = 0,		/* cannot be masked */
	DL_ERR = 0x1,		/* errors that should never happen */
	DL_WARN = 0x2,		/* something questionable.
				   recoverable errors */
	DL_NOTE = 0x4,		/* infrequent, important debug info */
	DL_INFO = 0x8,		/* debug information */
	DL_VERBOSE = 0x10,	/* detailed info, such as buffer traces */
	BE_DL_MIN_VALUE = 0x1,	/* this is the min value used */
	BE_DL_MAX_VALUE = 0x80	/* this is the higheset value used */
} ;

extern unsigned int trace_level;

#define TRACE(lm, fmt, args...)  {				\
		if (trace_level & lm) {				\
			printk(KERN_NOTICE "BE: %s:%d \n" fmt,	\
			__FILE__ , __LINE__ , ## args);		\
		}						\
	}

static inline unsigned int be_trace_set_level(unsigned int level)
{
	unsigned int old_level = trace_level;
	trace_level = level;
	return old_level;
}

#define be_trace_get_level() 	trace_level
/*
 * Returns number of pages spanned by the size of data
 * starting at the given address.
 */
#define PAGES_SPANNED(_address, _size) \
   ((u32)((((size_t)(_address) & (PAGE_SIZE - 1)) + \
		(_size) + (PAGE_SIZE - 1)) >> PAGE_SHIFT))
/* Byte offset into the page corresponding to given address */
#define OFFSET_IN_PAGE(_addr_) ((size_t)(_addr_) & (PAGE_SIZE-1))

/*
 * circular subtract.
 * Returns a - b assuming a circular number system, where a and b are
 * in range (0, maxValue-1). If a==b, zero is returned so the
 * highest value possible with this subtraction is maxValue-1.
 */
static inline u32 be_subc(u32 a, u32 b, u32 max)
{
	ASSERT(a <= max && b <= max);
	ASSERT(max > 0);
	return a >= b ? (a - b) : (max - b + a);
}

static inline u32 be_addc(u32 a, u32 b, u32 max)
{
	ASSERT(a < max);
	ASSERT(max > 0);
	return (max - a > b) ? (a + b) : (b + a - max);
}

/* descriptor for a physically contiguous memory used for ring */
struct ring_desc {
	u32 length;	/* length in bytes */
	void *va; 	/* virtual address */
	u64 pa;		/* bus address */
} ;

/*
 * This structure stores information about a ring shared between hardware
 * and software.  Each ring is allocated by the driver in the uncached
 * extension and mapped into BladeEngine's unified table.
 */
struct mp_ring {
	u32 pages;		/* queue size in pages */
	u32 id;			/* queue id assigned by beklib */
	u32 num;		/* number of elements in queue */
	u32 cidx;		/* consumer index */
	u32 pidx;		/* producer index -- not used by most rings */
	u32 itemSize;		/* size in bytes of one object */

	void *va;		/* The virtual address of the ring.
				   This should be last to allow 32 & 64
				   bit debugger extensions to work. */
} ;

/*-----------  amap bit filed get / set macros and functions -----*/
/*
 * Structures defined in the map header files (under fw/amap/) with names
 * in the format BE_<name>_AMAP are pseudo structures with members
 * of type u8. These structures are templates that are used in
 * conjuntion with the structures with names in the format
 * <name>_AMAP to calculate the bit masks and bit offsets to get or set
 * bit fields in structures. The structures <name>_AMAP are arrays
 * of 32 bits words and have the correct size.  The following macros
 * provide convenient ways to get and set the various members
 * in the structures without using strucctures with bit fields.
 * Always use the macros AMAP_GET_BITS_PTR and AMAP_SET_BITS_PTR
 * macros to extract and set various members.
 */

/*
 * Returns the a bit mask for the register that is NOT shifted into location.
 * That means return values always look like: 0x1, 0xFF, 0x7FF, etc...
 */
static inline u32 amap_mask(u32 bit_size)
{
	return bit_size == 32 ? 0xFFFFFFFF : (1 << bit_size) - 1;
}

#define AMAP_BIT_MASK(_struct_, field)       \
	amap_mask(AMAP_BIT_SIZE(_struct_, field))

/*
 * non-optimized set bits function. First clears the bits and then assigns them.
 * This does not require knowledge of the particular DWORD you are setting.
 * e.g. AMAP_SET_BITS_PTR (struct, field1, &contextMemory, 123);
 */
static inline void
amap_set(void *ptr, u32 dw_offset, u32 mask, u32 offset, u32 value)
{
	u32 *dw = (u32 *)ptr;
	*(dw + dw_offset) &= ~(mask << offset);
	*(dw + dw_offset) |= (mask & value) << offset;
}

#define AMAP_SET_BITS_PTR(_struct_, field, _structPtr_, val)	\
	amap_set(_structPtr_, AMAP_WORD_OFFSET(_struct_, field),\
		AMAP_BIT_MASK(_struct_, field),			\
		AMAP_BIT_OFFSET(_struct_, field), val)
/*
 * Non-optimized routine that gets the bits without knowing the correct DWORD.
 * e.g. fieldValue = AMAP_GET_BITS_PTR (struct, field1, &contextMemory);
 */
static inline u32
amap_get(void *ptr, u32 dw_offset, u32 mask, u32 offset)
{
	u32 *dw = (u32 *)ptr;
	return mask & (*(dw + dw_offset) >> offset);
}
#define AMAP_GET_BITS_PTR(_struct_, field, _structPtr_)			\
	amap_get(_structPtr_, AMAP_WORD_OFFSET(_struct_, field),	\
		AMAP_BIT_MASK(_struct_, field),				\
		AMAP_BIT_OFFSET(_struct_, field))

/* Returns 0-31 representing bit offset within a DWORD of a bitfield. */
#define AMAP_BIT_OFFSET(_struct_, field)                  \
	(offsetof(struct BE_ ## _struct_ ## _AMAP, field) % 32)

/* Returns 0-n representing DWORD offset of bitfield within the structure. */
#define AMAP_WORD_OFFSET(_struct_, field)  \
		  (offsetof(struct BE_ ## _struct_ ## _AMAP, field)/32)

/* Returns size of bitfield in bits. */
#define AMAP_BIT_SIZE(_struct_, field) \
		sizeof(((struct BE_ ## _struct_ ## _AMAP*)0)->field)

struct be_mcc_wrb_response_copy {
	u16 length;		/* bytes in response */
	u16 fwcmd_offset;	/* offset within the wrb of the response */
	void *va;		/* user's va to copy response into */

} ;
typedef void (*mcc_wrb_cqe_callback) (void *context, int status,
				struct MCC_WRB_AMAP *optional_wrb);
struct be_mcc_wrb_context {

	mcc_wrb_cqe_callback internal_cb;	/* Function to call on
						completion */
	void *internal_cb_context;	/* Parameter to pass
						   to completion function */

	mcc_wrb_cqe_callback cb;	/* Function to call on completion */
	void *cb_context;	/* Parameter to pass to completion function */

	int *users_final_status;	/* pointer to a local
						variable for synchronous
						commands */
	struct MCC_WRB_AMAP *wrb;	/* pointer to original wrb for embedded
						commands only */
	struct list_head next;	/* links context structs together in
				   free list */

	struct be_mcc_wrb_response_copy copy;	/* Optional parameters to copy
					   embedded response to user's va */

#if defined(BE_DEBUG)
	u16 subsystem, opcode;	/* Track this FWCMD for debug builds. */
	struct MCC_WRB_AMAP *ring_wrb;
	u32 consumed_count;
#endif
} ;

/*
    Represents a function object for network or storage.  This
    is used to manage per-function resources like MCC CQs, etc.
*/
struct be_function_object {

	u32 magic;		/*!< magic for detecting memory corruption. */

	/* PCI BAR mapped addresses */
	u8 __iomem *csr_va;	/* CSR */
	u8 __iomem *db_va;	/* Door Bell */
	u8 __iomem *pci_va;	/* PCI config space */
	u32 emulate;		/* if set, MPU is not available.
				  Emulate everything.     */
	u32 pend_queue_driving;	/* if set, drive the queued WRBs
				   after releasing the WRB lock */

	spinlock_t post_lock;	/* lock for verifying one thread posting wrbs */
	spinlock_t cq_lock;	/* lock for verifying one thread
				   processing cq */
	spinlock_t mcc_context_lock;	/* lock for protecting mcc
					   context free list */
	unsigned long post_irq;
	unsigned long cq_irq;

	u32 type;
	u32 pci_function_number;

	struct be_mcc_object *mcc;	/* mcc rings. */

	struct {
		struct MCC_MAILBOX_AMAP *va;	/* VA to the mailbox */
		u64 pa;	/* PA to the mailbox */
		u32 length;	/* byte length of mailbox */

		/* One default context struct used for posting at
		 * least one MCC_WRB
		 */
		struct be_mcc_wrb_context default_context;
		bool default_context_allocated;
	} mailbox;

	struct {

		/* Wake on lans configured. */
		u32 wol_bitmask;	/* bits 0,1,2,3 are set if
					   corresponding index is enabled */
	} config;


	struct BE_FIRMWARE_CONFIG fw_config;
} ;

/*
      Represents an Event Queue
*/
struct be_eq_object {
	u32 magic;
	atomic_t ref_count;

	struct be_function_object *parent_function;

	struct list_head eq_list;
	struct list_head cq_list_head;

	u32 eq_id;
	void *cb_context;

} ;

/*
    Manages a completion queue
*/
struct be_cq_object {
	u32 magic;
	atomic_t ref_count;

	struct be_function_object *parent_function;
	struct be_eq_object *eq_object;

	struct list_head cq_list;
	struct list_head cqlist_for_eq;

	void *va;
	u32 num_entries;

	void *cb_context;

	u32 cq_id;

} ;

/*
    Manages an ethernet send queue
*/
struct be_ethsq_object {
	u32 magic;

	struct list_head list;

	struct be_function_object *parent_function;
	struct be_cq_object *cq_object;
	u32 bid;

} ;

/*
@brief
    Manages an ethernet receive queue
*/
struct be_ethrq_object {
	u32 magic;
	struct list_head list;
	struct be_function_object *parent_function;
	u32 rid;
	struct be_cq_object *cq_object;
	struct be_cq_object *rss_cq_object[4];

} ;

/*
    Manages an MCC
*/
typedef void (*mcc_async_event_callback) (void *context, u32 event_code,
				void *event);
struct be_mcc_object {
	u32 magic;

	struct be_function_object *parent_function;
	struct list_head mcc_list;

	struct be_cq_object *cq_object;

	/* Async event callback for MCC CQ. */
	mcc_async_event_callback async_cb;
	void *async_context;

	struct {
		struct be_mcc_wrb_context *base;
		u32 num;
		struct list_head list_head;
	} wrb_context;

	struct {
		struct ring_desc *rd;
		struct mp_ring ring;
	} sq;

	struct {
		struct mp_ring ring;
	} cq;

	u32 processing;		/* flag indicating that one thread
				   is processing CQ */
	u32 rearm;		/* doorbell rearm setting to make
				   sure the active processing thread */
	/* rearms the CQ if any of the threads requested it. */

	struct list_head backlog;
	u32 backlog_length;
	u32 driving_backlog;
	u32 consumed_index;

} ;


/* Queue context header -- the required software information for
 * queueing a WRB.
 */
struct be_queue_driver_context {
	mcc_wrb_cqe_callback internal_cb;	/* Function to call on
						   completion */
	void *internal_cb_context;	/* Parameter to pass
						   to completion function */

	mcc_wrb_cqe_callback cb;	/* Function to call on completion */
	void *cb_context;	/* Parameter to pass to completion function */

	struct be_mcc_wrb_response_copy copy;	/* Optional parameters to copy
					   embedded response to user's va */
	void *optional_fwcmd_va;
	struct list_head list;
	u32 bytes;
} ;

/*
 * Common MCC WRB header that all commands require.
 */
struct be_mcc_wrb_header {
	u8 rsvd[offsetof(struct BE_MCC_WRB_AMAP, payload)/8];
} ;

/*
 * All non embedded commands supported by hwlib functions only allow
 * 1 SGE.  This queue context handles them all.
 */
struct be_nonembedded_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct MCC_SGE_AMAP sge[1];
} ;

/*
 * ------------------------------------------------------------------------
 *  This section contains the specific queue struct for each command.
 *  The user could always provide a be_generic_q_ctxt but this is a
 *  rather large struct.  By using the specific struct, memory consumption
 *  can be reduced.
 * ------------------------------------------------------------------------
 */

struct be_link_status_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_COMMON_NTWK_LINK_STATUS_QUERY fwcmd;
} ;

struct be_multicast_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_COMMON_NTWK_MULTICAST_SET fwcmd;
} ;


struct be_vlan_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_COMMON_NTWK_VLAN_CONFIG fwcmd;
} ;

struct be_promiscuous_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_ETH_PROMISCUOUS fwcmd;
} ;

struct be_force_failover_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_COMMON_FORCE_FAILOVER fwcmd;
} ;


struct be_rxf_filter_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_COMMON_NTWK_RX_FILTER fwcmd;
} ;

struct be_eq_modify_delay_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct FWCMD_COMMON_MODIFY_EQ_DELAY fwcmd;
} ;

/*
 * The generic context is the largest size that would be required.
 * It is the software context plus an entire WRB.
 */
struct be_generic_q_ctxt {
	struct be_queue_driver_context context;
	struct be_mcc_wrb_header wrb_header;
	struct MCC_WRB_PAYLOAD_AMAP payload;
} ;

/*
 * Types for the BE_QUEUE_CONTEXT object.
 */
#define BE_QUEUE_INVALID	(0)
#define BE_QUEUE_LINK_STATUS	(0xA006)
#define BE_QUEUE_ETH_STATS	(0xA007)
#define BE_QUEUE_TPM_STATS	(0xA008)
#define BE_QUEUE_TCP_STATS	(0xA009)
#define BE_QUEUE_MULTICAST	(0xA00A)
#define BE_QUEUE_VLAN		(0xA00B)
#define BE_QUEUE_RSS		(0xA00C)
#define BE_QUEUE_FORCE_FAILOVER	(0xA00D)
#define BE_QUEUE_PROMISCUOUS	(0xA00E)
#define BE_QUEUE_WAKE_ON_LAN	(0xA00F)
#define BE_QUEUE_NOP		(0xA010)

/* --- BE_FUNCTION_ENUM --- */
#define BE_FUNCTION_TYPE_ISCSI          (0)
#define BE_FUNCTION_TYPE_NETWORK        (1)
#define BE_FUNCTION_TYPE_ARM            (2)

/* --- BE_ETH_TX_RING_TYPE_ENUM --- */
#define BE_ETH_TX_RING_TYPE_FORWARDING  (1) 	/* Ether ring for forwarding */
#define BE_ETH_TX_RING_TYPE_STANDARD    (2)	/* Ether ring for sending */
						/* network packets. */
#define BE_ETH_TX_RING_TYPE_BOUND       (3)	/* Ethernet ring for sending */
						/* network packets, bound */
						/* to a physical port. */
/*
 * ----------------------------------------------------------------------
 *   API MACROS
 * ----------------------------------------------------------------------
 */
#define BE_FWCMD_NAME(_short_name_)     struct FWCMD_##_short_name_
#define BE_OPCODE_NAME(_short_name_)    OPCODE_##_short_name_
#define BE_SUBSYSTEM_NAME(_short_name_) SUBSYSTEM_##_short_name_


#define BE_PREPARE_EMBEDDED_FWCMD(_pfob_, _wrb_, _short_name_)	\
	((BE_FWCMD_NAME(_short_name_) *)				\
	be_function_prepare_embedded_fwcmd(_pfob_, _wrb_,	\
		sizeof(BE_FWCMD_NAME(_short_name_)),		\
		FIELD_SIZEOF(BE_FWCMD_NAME(_short_name_), params.request), \
		FIELD_SIZEOF(BE_FWCMD_NAME(_short_name_), params.response), \
		BE_OPCODE_NAME(_short_name_),				\
		BE_SUBSYSTEM_NAME(_short_name_)));

#define BE_PREPARE_NONEMBEDDED_FWCMD(_pfob_, _wrb_, _iva_, _ipa_, _short_name_)\
	((BE_FWCMD_NAME(_short_name_) *)				\
	be_function_prepare_nonembedded_fwcmd(_pfob_, _wrb_, (_iva_), (_ipa_), \
		sizeof(BE_FWCMD_NAME(_short_name_)),		\
		FIELD_SIZEOF(BE_FWCMD_NAME(_short_name_), params.request), \
		FIELD_SIZEOF(BE_FWCMD_NAME(_short_name_), params.response), \
		BE_OPCODE_NAME(_short_name_),				\
		BE_SUBSYSTEM_NAME(_short_name_)));

int be_function_object_create(u8 __iomem *csr_va, u8 __iomem *db_va,
	u8 __iomem *pci_va, u32 function_type, struct ring_desc *mailbox_rd,
	  struct be_function_object *pfob);

int be_function_object_destroy(struct be_function_object *pfob);
int be_function_cleanup(struct be_function_object *pfob);


int be_function_get_fw_version(struct be_function_object *pfob,
	struct FWCMD_COMMON_GET_FW_VERSION_RESPONSE_PAYLOAD *fw_version,
	mcc_wrb_cqe_callback cb, void *cb_context);


int be_eq_modify_delay(struct be_function_object *pfob,
		   u32 num_eq, struct be_eq_object **eq_array,
		   u32 *eq_delay_array, mcc_wrb_cqe_callback cb,
		   void *cb_context,
		   struct be_eq_modify_delay_q_ctxt *q_ctxt);



int be_eq_create(struct be_function_object *pfob,
	     struct ring_desc *rd, u32 eqe_size, u32 num_entries,
	     u32 watermark, u32 timer_delay, struct be_eq_object *eq_object);

int be_eq_destroy(struct be_eq_object *eq);

int be_cq_create(struct be_function_object *pfob,
	struct ring_desc *rd, u32 length,
	bool solicited_eventable, bool no_delay,
	u32 wm_thresh, struct be_eq_object *eq_object,
	struct be_cq_object *cq_object);

int be_cq_destroy(struct be_cq_object *cq);

int be_mcc_ring_create(struct be_function_object *pfob,
		   struct ring_desc *rd, u32 length,
		   struct be_mcc_wrb_context *context_array,
		   u32 num_context_entries,
		   struct be_cq_object *cq, struct be_mcc_object *mcc);
int be_mcc_ring_destroy(struct be_mcc_object *mcc_object);

int be_mcc_process_cq(struct be_mcc_object *mcc_object, bool rearm);

int be_mcc_add_async_event_callback(struct be_mcc_object *mcc_object,
		mcc_async_event_callback cb, void *cb_context);

int be_pci_soft_reset(struct be_function_object *pfob);


int be_drive_POST(struct be_function_object *pfob);


int be_eth_sq_create(struct be_function_object *pfob,
		struct ring_desc *rd, u32 length_in_bytes,
		u32 type, u32 ulp, struct be_cq_object *cq_object,
		struct be_ethsq_object *eth_sq);

struct be_eth_sq_parameters {
	u32 port;
	u32 rsvd0[2];
} ;

int be_eth_sq_create_ex(struct be_function_object *pfob,
		    struct ring_desc *rd, u32 length_in_bytes,
		    u32 type, u32 ulp, struct be_cq_object *cq_object,
		    struct be_eth_sq_parameters *ex_parameters,
		    struct be_ethsq_object *eth_sq);
int be_eth_sq_destroy(struct be_ethsq_object *eth_sq);

int be_eth_set_flow_control(struct be_function_object *pfob,
			bool txfc_enable, bool rxfc_enable);

int be_eth_get_flow_control(struct be_function_object *pfob,
			bool *txfc_enable, bool *rxfc_enable);
int be_eth_set_qos(struct be_function_object *pfob, u32 max_bps, u32 max_pps);

int be_eth_get_qos(struct be_function_object *pfob, u32 *max_bps, u32 *max_pps);

int be_eth_set_frame_size(struct be_function_object *pfob,
		      u32 *tx_frame_size, u32 *rx_frame_size);

int be_eth_rq_create(struct be_function_object *pfob,
		 struct ring_desc *rd, struct be_cq_object *cq_object,
		 struct be_cq_object *bcmc_cq_object,
		 struct be_ethrq_object *eth_rq);

int be_eth_rq_destroy(struct be_ethrq_object *eth_rq);

int be_eth_rq_destroy_options(struct be_ethrq_object *eth_rq, bool flush,
		mcc_wrb_cqe_callback cb, void *cb_context);
int be_eth_rq_set_frag_size(struct be_function_object *pfob,
		u32 new_frag_size_bytes, u32 *actual_frag_size_bytes);
int be_eth_rq_get_frag_size(struct be_function_object *pfob,
						u32 *frag_size_bytes);

void *be_function_prepare_embedded_fwcmd(struct be_function_object *pfob,
		   struct MCC_WRB_AMAP *wrb,
		   u32 payload_length, u32 request_length,
		   u32 response_length, u32 opcode, u32 subsystem);
void *be_function_prepare_nonembedded_fwcmd(struct be_function_object *pfob,
	struct MCC_WRB_AMAP *wrb, void *fwcmd_header_va, u64 fwcmd_header_pa,
	u32 payload_length, u32 request_length, u32 response_length,
	u32 opcode, u32 subsystem);


struct MCC_WRB_AMAP *
be_function_peek_mcc_wrb(struct be_function_object *pfob);

int be_rxf_mac_address_read_write(struct be_function_object *pfob,
	      bool port1, bool mac1, bool mgmt,
	      bool write, bool permanent, u8 *mac_address,
	      mcc_wrb_cqe_callback cb,
	      void *cb_context);

int be_rxf_multicast_config(struct be_function_object *pfob,
			bool promiscuous, u32 num, u8 *mac_table,
			mcc_wrb_cqe_callback cb,
			void *cb_context,
			struct be_multicast_q_ctxt *q_ctxt);

int be_rxf_vlan_config(struct be_function_object *pfob,
	   bool promiscuous, u32 num, u16 *vlan_tag_array,
	   mcc_wrb_cqe_callback cb, void *cb_context,
	   struct be_vlan_q_ctxt *q_ctxt);


int be_rxf_link_status(struct be_function_object *pfob,
		   struct BE_LINK_STATUS *link_status,
		   mcc_wrb_cqe_callback cb,
		   void *cb_context,
		   struct be_link_status_q_ctxt *q_ctxt);


int be_rxf_query_eth_statistics(struct be_function_object *pfob,
		struct FWCMD_ETH_GET_STATISTICS *va_for_fwcmd,
		u64 pa_for_fwcmd, mcc_wrb_cqe_callback cb,
		void *cb_context,
		struct be_nonembedded_q_ctxt *q_ctxt);

int be_rxf_promiscuous(struct be_function_object *pfob,
		   bool enable_port0, bool enable_port1,
		   mcc_wrb_cqe_callback cb, void *cb_context,
		   struct be_promiscuous_q_ctxt *q_ctxt);


int be_rxf_filter_config(struct be_function_object *pfob,
		     struct NTWK_RX_FILTER_SETTINGS *settings,
		     mcc_wrb_cqe_callback cb,
		     void *cb_context,
		     struct be_rxf_filter_q_ctxt *q_ctxt);

/*
 * ------------------------------------------------------
 *  internal functions used by hwlib
 * ------------------------------------------------------
 */


int be_function_ring_destroy(struct be_function_object *pfob,
		       u32 id, u32 ring_type, mcc_wrb_cqe_callback cb,
		       void *cb_context,
		       mcc_wrb_cqe_callback internal_cb,
		       void *internal_callback_context);

int be_function_post_mcc_wrb(struct be_function_object *pfob,
		struct MCC_WRB_AMAP *wrb,
		struct be_generic_q_ctxt *q_ctxt,
		mcc_wrb_cqe_callback cb, void *cb_context,
		mcc_wrb_cqe_callback internal_cb,
		void *internal_cb_context, void *optional_fwcmd_va,
		struct be_mcc_wrb_response_copy *response_copy);

int be_function_queue_mcc_wrb(struct be_function_object *pfob,
			  struct be_generic_q_ctxt *q_ctxt);

/*
 * ------------------------------------------------------
 *  MCC QUEUE
 * ------------------------------------------------------
 */

int be_mpu_init_mailbox(struct be_function_object *pfob, struct ring_desc *rd);


struct MCC_WRB_AMAP *
_be_mpu_peek_ring_wrb(struct be_mcc_object *mcc, bool driving_queue);

struct be_mcc_wrb_context *
_be_mcc_allocate_wrb_context(struct be_function_object *pfob);

void _be_mcc_free_wrb_context(struct be_function_object *pfob,
			 struct be_mcc_wrb_context *context);

int _be_mpu_post_wrb_mailbox(struct be_function_object *pfob,
	 struct MCC_WRB_AMAP *wrb, struct be_mcc_wrb_context *wrb_context);

int _be_mpu_post_wrb_ring(struct be_mcc_object *mcc,
	struct MCC_WRB_AMAP *wrb, struct be_mcc_wrb_context *wrb_context);

void be_drive_mcc_wrb_queue(struct be_mcc_object *mcc);


/*
 * ------------------------------------------------------
 *  Ring Sizes
 * ------------------------------------------------------
 */
static inline u32 be_ring_encoding_to_length(u32 encoding, u32 object_size)
{

	ASSERT(encoding != 1);	/* 1 is rsvd */
	ASSERT(encoding < 16);
	ASSERT(object_size > 0);

	if (encoding == 0)	/* 32k deep */
		encoding = 16;

	return (1 << (encoding - 1)) * object_size;
}

static inline
u32 be_ring_length_to_encoding(u32 length_in_bytes, u32 object_size)
{

	u32 count, encoding;

	ASSERT(object_size > 0);
	ASSERT(length_in_bytes % object_size == 0);

	count = length_in_bytes / object_size;

	ASSERT(count > 1);
	ASSERT(count <= 32 * 1024);
	ASSERT(length_in_bytes <= 8 * PAGE_SIZE); /* max ring size in UT */

	encoding = __ilog2_u32(count) + 1;

	if (encoding == 16)
		encoding = 0;	/* 32k deep */

	return encoding;
}

void be_rd_to_pa_list(struct ring_desc *rd, struct PHYS_ADDR *pa_list,
						u32 max_num);
#endif /* __hwlib_h__ */
