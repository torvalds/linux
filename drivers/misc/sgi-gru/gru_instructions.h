/*
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __GRU_INSTRUCTIONS_H__
#define __GRU_INSTRUCTIONS_H__

extern int gru_check_status_proc(void *cb);
extern int gru_wait_proc(void *cb);
extern void gru_wait_abort_proc(void *cb);



/*
 * Architecture dependent functions
 */

#if defined(CONFIG_IA64)
#include <linux/compiler.h>
#include <asm/intrinsics.h>
#define __flush_cache(p)		ia64_fc((unsigned long)p)
/* Use volatile on IA64 to ensure ordering via st4.rel */
#define gru_ordered_store_int(p, v)					\
		do {							\
			barrier();					\
			*((volatile int *)(p)) = v; /* force st.rel */	\
		} while (0)
#elif defined(CONFIG_X86_64)
#define __flush_cache(p)		clflush(p)
#define gru_ordered_store_int(p, v)					\
		do {							\
			barrier();					\
			*(int *)p = v;					\
		} while (0)
#else
#error "Unsupported architecture"
#endif

/*
 * Control block status and exception codes
 */
#define CBS_IDLE			0
#define CBS_EXCEPTION			1
#define CBS_ACTIVE			2
#define CBS_CALL_OS			3

/* CB substatus bitmasks */
#define CBSS_MSG_QUEUE_MASK		7
#define CBSS_IMPLICIT_ABORT_ACTIVE_MASK	8

/* CB substatus message queue values (low 3 bits of substatus) */
#define CBSS_NO_ERROR			0
#define CBSS_LB_OVERFLOWED		1
#define CBSS_QLIMIT_REACHED		2
#define CBSS_PAGE_OVERFLOW		3
#define CBSS_AMO_NACKED			4
#define CBSS_PUT_NACKED			5

/*
 * Structure used to fetch exception detail for CBs that terminate with
 * CBS_EXCEPTION
 */
struct control_block_extended_exc_detail {
	unsigned long	cb;
	int		opc;
	int		ecause;
	int		exopc;
	long		exceptdet0;
	int		exceptdet1;
	int		cbrstate;
	int		cbrexecstatus;
};

/*
 * Instruction formats
 */

/*
 * Generic instruction format.
 * This definition has precise bit field definitions.
 */
struct gru_instruction_bits {
    /* DW 0  - low */
    unsigned int		icmd:      1;
    unsigned char		ima:	   3;	/* CB_DelRep, unmapped mode */
    unsigned char		reserved0: 4;
    unsigned int		xtype:     3;
    unsigned int		iaa0:      2;
    unsigned int		iaa1:      2;
    unsigned char		reserved1: 1;
    unsigned char		opc:       8;	/* opcode */
    unsigned char		exopc:     8;	/* extended opcode */
    /* DW 0  - high */
    unsigned int		idef2:    22;	/* TRi0 */
    unsigned char		reserved2: 2;
    unsigned char		istatus:   2;
    unsigned char		isubstatus:4;
    unsigned char		reserved3: 1;
    unsigned char		tlb_fault_color: 1;
    /* DW 1 */
    unsigned long		idef4;		/* 42 bits: TRi1, BufSize */
    /* DW 2-6 */
    unsigned long		idef1;		/* BAddr0 */
    unsigned long		idef5;		/* Nelem */
    unsigned long		idef6;		/* Stride, Operand1 */
    unsigned long		idef3;		/* BAddr1, Value, Operand2 */
    unsigned long		reserved4;
    /* DW 7 */
    unsigned long		avalue;		 /* AValue */
};

/*
 * Generic instruction with friendlier names. This format is used
 * for inline instructions.
 */
struct gru_instruction {
    /* DW 0 */
    unsigned int		op32;    /* icmd,xtype,iaa0,ima,opc */
    unsigned int		tri0;
    unsigned long		tri1_bufsize;		/* DW 1 */
    unsigned long		baddr0;			/* DW 2 */
    unsigned long		nelem;			/* DW 3 */
    unsigned long		op1_stride;		/* DW 4 */
    unsigned long		op2_value_baddr1;	/* DW 5 */
    unsigned long		reserved0;		/* DW 6 */
    unsigned long		avalue;			/* DW 7 */
};

/* Some shifts and masks for the low 32 bits of a GRU command */
#define GRU_CB_ICMD_SHFT	0
#define GRU_CB_ICMD_MASK	0x1
#define GRU_CB_XTYPE_SHFT	8
#define GRU_CB_XTYPE_MASK	0x7
#define GRU_CB_IAA0_SHFT	11
#define GRU_CB_IAA0_MASK	0x3
#define GRU_CB_IAA1_SHFT	13
#define GRU_CB_IAA1_MASK	0x3
#define GRU_CB_IMA_SHFT		1
#define GRU_CB_IMA_MASK		0x3
#define GRU_CB_OPC_SHFT		16
#define GRU_CB_OPC_MASK		0xff
#define GRU_CB_EXOPC_SHFT	24
#define GRU_CB_EXOPC_MASK	0xff

/* GRU instruction opcodes (opc field) */
#define OP_NOP		0x00
#define OP_BCOPY	0x01
#define OP_VLOAD	0x02
#define OP_IVLOAD	0x03
#define OP_VSTORE	0x04
#define OP_IVSTORE	0x05
#define OP_VSET		0x06
#define OP_IVSET	0x07
#define OP_MESQ		0x08
#define OP_GAMXR	0x09
#define OP_GAMIR	0x0a
#define OP_GAMIRR	0x0b
#define OP_GAMER	0x0c
#define OP_GAMERR	0x0d
#define OP_BSTORE	0x0e
#define OP_VFLUSH	0x0f


/* Extended opcodes values (exopc field) */

/* GAMIR - AMOs with implicit operands */
#define EOP_IR_FETCH	0x01 /* Plain fetch of memory */
#define EOP_IR_CLR	0x02 /* Fetch and clear */
#define EOP_IR_INC	0x05 /* Fetch and increment */
#define EOP_IR_DEC	0x07 /* Fetch and decrement */
#define EOP_IR_QCHK1	0x0d /* Queue check, 64 byte msg */
#define EOP_IR_QCHK2	0x0e /* Queue check, 128 byte msg */

/* GAMIRR - Registered AMOs with implicit operands */
#define EOP_IRR_FETCH	0x01 /* Registered fetch of memory */
#define EOP_IRR_CLR	0x02 /* Registered fetch and clear */
#define EOP_IRR_INC	0x05 /* Registered fetch and increment */
#define EOP_IRR_DEC	0x07 /* Registered fetch and decrement */
#define EOP_IRR_DECZ	0x0f /* Registered fetch and decrement, update on zero*/

/* GAMER - AMOs with explicit operands */
#define EOP_ER_SWAP	0x00 /* Exchange argument and memory */
#define EOP_ER_OR	0x01 /* Logical OR with memory */
#define EOP_ER_AND	0x02 /* Logical AND with memory */
#define EOP_ER_XOR	0x03 /* Logical XOR with memory */
#define EOP_ER_ADD	0x04 /* Add value to memory */
#define EOP_ER_CSWAP	0x08 /* Compare with operand2, write operand1 if match*/
#define EOP_ER_CADD	0x0c /* Queue check, operand1*64 byte msg */

/* GAMERR - Registered AMOs with explicit operands */
#define EOP_ERR_SWAP	0x00 /* Exchange argument and memory */
#define EOP_ERR_OR	0x01 /* Logical OR with memory */
#define EOP_ERR_AND	0x02 /* Logical AND with memory */
#define EOP_ERR_XOR	0x03 /* Logical XOR with memory */
#define EOP_ERR_ADD	0x04 /* Add value to memory */
#define EOP_ERR_CSWAP	0x08 /* Compare with operand2, write operand1 if match*/
#define EOP_ERR_EPOLL	0x09 /* Poll for equality */
#define EOP_ERR_NPOLL	0x0a /* Poll for inequality */

/* GAMXR - SGI Arithmetic unit */
#define EOP_XR_CSWAP	0x0b /* Masked compare exchange */


/* Transfer types (xtype field) */
#define XTYPE_B		0x0	/* byte */
#define XTYPE_S		0x1	/* short (2-byte) */
#define XTYPE_W		0x2	/* word (4-byte) */
#define XTYPE_DW	0x3	/* doubleword (8-byte) */
#define XTYPE_CL	0x6	/* cacheline (64-byte) */


/* Instruction access attributes (iaa0, iaa1 fields) */
#define IAA_RAM		0x0	/* normal cached RAM access */
#define IAA_NCRAM	0x2	/* noncoherent RAM access */
#define IAA_MMIO	0x1	/* noncoherent memory-mapped I/O space */
#define IAA_REGISTER	0x3	/* memory-mapped registers, etc. */


/* Instruction mode attributes (ima field) */
#define IMA_MAPPED	0x0	/* Virtual mode  */
#define IMA_CB_DELAY	0x1	/* hold read responses until status changes */
#define IMA_UNMAPPED	0x2	/* bypass the TLBs (OS only) */
#define IMA_INTERRUPT	0x4	/* Interrupt when instruction completes */

/* CBE ecause bits */
#define CBE_CAUSE_RI				(1 << 0)
#define CBE_CAUSE_INVALID_INSTRUCTION		(1 << 1)
#define CBE_CAUSE_UNMAPPED_MODE_FORBIDDEN	(1 << 2)
#define CBE_CAUSE_PE_CHECK_DATA_ERROR		(1 << 3)
#define CBE_CAUSE_IAA_GAA_MISMATCH		(1 << 4)
#define CBE_CAUSE_DATA_SEGMENT_LIMIT_EXCEPTION	(1 << 5)
#define CBE_CAUSE_OS_FATAL_TLB_FAULT		(1 << 6)
#define CBE_CAUSE_EXECUTION_HW_ERROR		(1 << 7)
#define CBE_CAUSE_TLBHW_ERROR			(1 << 8)
#define CBE_CAUSE_RA_REQUEST_TIMEOUT		(1 << 9)
#define CBE_CAUSE_HA_REQUEST_TIMEOUT		(1 << 10)
#define CBE_CAUSE_RA_RESPONSE_FATAL		(1 << 11)
#define CBE_CAUSE_RA_RESPONSE_NON_FATAL		(1 << 12)
#define CBE_CAUSE_HA_RESPONSE_FATAL		(1 << 13)
#define CBE_CAUSE_HA_RESPONSE_NON_FATAL		(1 << 14)
#define CBE_CAUSE_ADDRESS_SPACE_DECODE_ERROR	(1 << 15)
#define CBE_CAUSE_PROTOCOL_STATE_DATA_ERROR	(1 << 16)
#define CBE_CAUSE_RA_RESPONSE_DATA_ERROR	(1 << 17)
#define CBE_CAUSE_HA_RESPONSE_DATA_ERROR	(1 << 18)

/* CBE cbrexecstatus bits */
#define CBR_EXS_ABORT_OCC_BIT			0
#define CBR_EXS_INT_OCC_BIT			1
#define CBR_EXS_PENDING_BIT			2
#define CBR_EXS_QUEUED_BIT			3
#define CBR_EXS_TLB_INVAL_BIT			4
#define CBR_EXS_EXCEPTION_BIT			5

#define CBR_EXS_ABORT_OCC			(1 << CBR_EXS_ABORT_OCC_BIT)
#define CBR_EXS_INT_OCC				(1 << CBR_EXS_INT_OCC_BIT)
#define CBR_EXS_PENDING				(1 << CBR_EXS_PENDING_BIT)
#define CBR_EXS_QUEUED				(1 << CBR_EXS_QUEUED_BIT)
#define CBR_TLB_INVAL				(1 << CBR_EXS_TLB_INVAL_BIT)
#define CBR_EXS_EXCEPTION			(1 << CBR_EXS_EXCEPTION_BIT)

/*
 * Exceptions are retried for the following cases. If any OTHER bits are set
 * in ecause, the exception is not retryable.
 */
#define EXCEPTION_RETRY_BITS (CBE_CAUSE_EXECUTION_HW_ERROR |		\
			      CBE_CAUSE_TLBHW_ERROR |			\
			      CBE_CAUSE_RA_REQUEST_TIMEOUT |		\
			      CBE_CAUSE_RA_RESPONSE_NON_FATAL |		\
			      CBE_CAUSE_HA_RESPONSE_NON_FATAL |		\
			      CBE_CAUSE_RA_RESPONSE_DATA_ERROR |	\
			      CBE_CAUSE_HA_RESPONSE_DATA_ERROR		\
			      )

/* Message queue head structure */
union gru_mesqhead {
	unsigned long	val;
	struct {
		unsigned int	head;
		unsigned int	limit;
	};
};


/* Generate the low word of a GRU instruction */
static inline unsigned int
__opword(unsigned char opcode, unsigned char exopc, unsigned char xtype,
       unsigned char iaa0, unsigned char iaa1,
       unsigned char ima)
{
    return (1 << GRU_CB_ICMD_SHFT) |
	   (iaa0 << GRU_CB_IAA0_SHFT) |
	   (iaa1 << GRU_CB_IAA1_SHFT) |
	   (ima << GRU_CB_IMA_SHFT) |
	   (xtype << GRU_CB_XTYPE_SHFT) |
	   (opcode << GRU_CB_OPC_SHFT) |
	   (exopc << GRU_CB_EXOPC_SHFT);
}

/*
 * Architecture specific intrinsics
 */
static inline void gru_flush_cache(void *p)
{
	__flush_cache(p);
}

/*
 * Store the lower 32 bits of the command including the "start" bit. Then
 * start the instruction executing.
 */
static inline void gru_start_instruction(struct gru_instruction *ins, int op32)
{
	gru_ordered_store_int(ins, op32);
	gru_flush_cache(ins);
}


/* Convert "hints" to IMA */
#define CB_IMA(h)		((h) | IMA_UNMAPPED)

/* Convert data segment cache line index into TRI0 / TRI1 value */
#define GRU_DINDEX(i)		((i) * GRU_CACHE_LINE_BYTES)

/* Inline functions for GRU instructions.
 *     Note:
 *     	- nelem and stride are in elements
 *     	- tri0/tri1 is in bytes for the beginning of the data segment.
 */
static inline void gru_vload(void *cb, unsigned long mem_addr,
		unsigned int tri0, unsigned char xtype, unsigned long nelem,
		unsigned long stride, unsigned long hints)
{
	struct gru_instruction *ins = (struct gru_instruction *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	ins->op1_stride = stride;
	gru_start_instruction(ins, __opword(OP_VLOAD, 0, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_vstore(void *cb, unsigned long mem_addr,
		unsigned int tri0, unsigned char xtype, unsigned long nelem,
		unsigned long stride, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	ins->op1_stride = stride;
	gru_start_instruction(ins, __opword(OP_VSTORE, 0, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_ivload(void *cb, unsigned long mem_addr,
		unsigned int tri0, unsigned int tri1, unsigned char xtype,
		unsigned long nelem, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	ins->tri1_bufsize = tri1;
	gru_start_instruction(ins, __opword(OP_IVLOAD, 0, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_ivstore(void *cb, unsigned long mem_addr,
		unsigned int tri0, unsigned int tri1,
		unsigned char xtype, unsigned long nelem, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	ins->tri1_bufsize = tri1;
	gru_start_instruction(ins, __opword(OP_IVSTORE, 0, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_vset(void *cb, unsigned long mem_addr,
		unsigned long value, unsigned char xtype, unsigned long nelem,
		unsigned long stride, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->op2_value_baddr1 = value;
	ins->nelem = nelem;
	ins->op1_stride = stride;
	gru_start_instruction(ins, __opword(OP_VSET, 0, xtype, IAA_RAM, 0,
					 CB_IMA(hints)));
}

static inline void gru_ivset(void *cb, unsigned long mem_addr,
		unsigned int tri1, unsigned long value, unsigned char xtype,
		unsigned long nelem, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->op2_value_baddr1 = value;
	ins->nelem = nelem;
	ins->tri1_bufsize = tri1;
	gru_start_instruction(ins, __opword(OP_IVSET, 0, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_vflush(void *cb, unsigned long mem_addr,
		unsigned long nelem, unsigned char xtype, unsigned long stride,
		unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)mem_addr;
	ins->op1_stride = stride;
	ins->nelem = nelem;
	gru_start_instruction(ins, __opword(OP_VFLUSH, 0, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_nop(void *cb, int hints)
{
	struct gru_instruction *ins = (void *)cb;

	gru_start_instruction(ins, __opword(OP_NOP, 0, 0, 0, 0, CB_IMA(hints)));
}


static inline void gru_bcopy(void *cb, const unsigned long src,
		unsigned long dest,
		unsigned int tri0, unsigned int xtype, unsigned long nelem,
		unsigned int bufsize, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	ins->op2_value_baddr1 = (long)dest;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	ins->tri1_bufsize = bufsize;
	gru_start_instruction(ins, __opword(OP_BCOPY, 0, xtype, IAA_RAM,
					IAA_RAM, CB_IMA(hints)));
}

static inline void gru_bstore(void *cb, const unsigned long src,
		unsigned long dest, unsigned int tri0, unsigned int xtype,
		unsigned long nelem, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	ins->op2_value_baddr1 = (long)dest;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	gru_start_instruction(ins, __opword(OP_BSTORE, 0, xtype, 0, IAA_RAM,
					CB_IMA(hints)));
}

static inline void gru_gamir(void *cb, int exopc, unsigned long src,
		unsigned int xtype, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	gru_start_instruction(ins, __opword(OP_GAMIR, exopc, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_gamirr(void *cb, int exopc, unsigned long src,
		unsigned int xtype, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	gru_start_instruction(ins, __opword(OP_GAMIRR, exopc, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_gamer(void *cb, int exopc, unsigned long src,
		unsigned int xtype,
		unsigned long operand1, unsigned long operand2,
		unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	ins->op1_stride = operand1;
	ins->op2_value_baddr1 = operand2;
	gru_start_instruction(ins, __opword(OP_GAMER, exopc, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_gamerr(void *cb, int exopc, unsigned long src,
		unsigned int xtype, unsigned long operand1,
		unsigned long operand2, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	ins->op1_stride = operand1;
	ins->op2_value_baddr1 = operand2;
	gru_start_instruction(ins, __opword(OP_GAMERR, exopc, xtype, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline void gru_gamxr(void *cb, unsigned long src,
		unsigned int tri0, unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)src;
	ins->nelem = 4;
	gru_start_instruction(ins, __opword(OP_GAMXR, EOP_XR_CSWAP, XTYPE_DW,
				 IAA_RAM, 0, CB_IMA(hints)));
}

static inline void gru_mesq(void *cb, unsigned long queue,
		unsigned long tri0, unsigned long nelem,
		unsigned long hints)
{
	struct gru_instruction *ins = (void *)cb;

	ins->baddr0 = (long)queue;
	ins->nelem = nelem;
	ins->tri0 = tri0;
	gru_start_instruction(ins, __opword(OP_MESQ, 0, XTYPE_CL, IAA_RAM, 0,
					CB_IMA(hints)));
}

static inline unsigned long gru_get_amo_value(void *cb)
{
	struct gru_instruction *ins = (void *)cb;

	return ins->avalue;
}

static inline int gru_get_amo_value_head(void *cb)
{
	struct gru_instruction *ins = (void *)cb;

	return ins->avalue & 0xffffffff;
}

static inline int gru_get_amo_value_limit(void *cb)
{
	struct gru_instruction *ins = (void *)cb;

	return ins->avalue >> 32;
}

static inline union gru_mesqhead  gru_mesq_head(int head, int limit)
{
	union gru_mesqhead mqh;

	mqh.head = head;
	mqh.limit = limit;
	return mqh;
}

/*
 * Get struct control_block_extended_exc_detail for CB.
 */
extern int gru_get_cb_exception_detail(void *cb,
		       struct control_block_extended_exc_detail *excdet);

#define GRU_EXC_STR_SIZE		256


/*
 * Control block definition for checking status
 */
struct gru_control_block_status {
	unsigned int	icmd		:1;
	unsigned int	ima		:3;
	unsigned int	reserved0	:4;
	unsigned int	unused1		:24;
	unsigned int	unused2		:24;
	unsigned int	istatus		:2;
	unsigned int	isubstatus	:4;
	unsigned int	unused3		:2;
};

/* Get CB status */
static inline int gru_get_cb_status(void *cb)
{
	struct gru_control_block_status *cbs = (void *)cb;

	return cbs->istatus;
}

/* Get CB message queue substatus */
static inline int gru_get_cb_message_queue_substatus(void *cb)
{
	struct gru_control_block_status *cbs = (void *)cb;

	return cbs->isubstatus & CBSS_MSG_QUEUE_MASK;
}

/* Get CB substatus */
static inline int gru_get_cb_substatus(void *cb)
{
	struct gru_control_block_status *cbs = (void *)cb;

	return cbs->isubstatus;
}

/*
 * User interface to check an instruction status. UPM and exceptions
 * are handled automatically. However, this function does NOT wait
 * for an active instruction to complete.
 *
 */
static inline int gru_check_status(void *cb)
{
	struct gru_control_block_status *cbs = (void *)cb;
	int ret;

	ret = cbs->istatus;
	if (ret != CBS_ACTIVE)
		ret = gru_check_status_proc(cb);
	return ret;
}

/*
 * User interface (via inline function) to wait for an instruction
 * to complete. Completion status (IDLE or EXCEPTION is returned
 * to the user. Exception due to hardware errors are automatically
 * retried before returning an exception.
 *
 */
static inline int gru_wait(void *cb)
{
	return gru_wait_proc(cb);
}

/*
 * Wait for CB to complete. Aborts program if error. (Note: error does NOT
 * mean TLB mis - only fatal errors such as memory parity error or user
 * bugs will cause termination.
 */
static inline void gru_wait_abort(void *cb)
{
	gru_wait_abort_proc(cb);
}


/*
 * Get a pointer to a control block
 * 	gseg	- GSeg address returned from gru_get_thread_gru_segment()
 * 	index	- index of desired CB
 */
static inline void *gru_get_cb_pointer(void *gseg,
						      int index)
{
	return gseg + GRU_CB_BASE + index * GRU_HANDLE_STRIDE;
}

/*
 * Get a pointer to a cacheline in the data segment portion of a GSeg
 * 	gseg	- GSeg address returned from gru_get_thread_gru_segment()
 * 	index	- index of desired cache line
 */
static inline void *gru_get_data_pointer(void *gseg, int index)
{
	return gseg + GRU_DS_BASE + index * GRU_CACHE_LINE_BYTES;
}

/*
 * Convert a vaddr into the tri index within the GSEG
 * 	vaddr		- virtual address of within gseg
 */
static inline int gru_get_tri(void *vaddr)
{
	return ((unsigned long)vaddr & (GRU_GSEG_PAGESIZE - 1)) - GRU_DS_BASE;
}
#endif		/* __GRU_INSTRUCTIONS_H__ */
