/*
 * Definitions for using the Apple Descriptor-Based DMA controller
 * in Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

#ifdef __KERNEL__
#ifndef _ASM_DBDMA_H_
#define _ASM_DBDMA_H_
/*
 * DBDMA control/status registers.  All little-endian.
 */
struct dbdma_regs {
    unsigned int control;	/* lets you change bits in status */
    unsigned int status;	/* DMA and device status bits (see below) */
    unsigned int cmdptr_hi;	/* upper 32 bits of command address */
    unsigned int cmdptr;	/* (lower 32 bits of) command address (phys) */
    unsigned int intr_sel;	/* select interrupt condition bit */
    unsigned int br_sel;	/* select branch condition bit */
    unsigned int wait_sel;	/* select wait condition bit */
    unsigned int xfer_mode;
    unsigned int data2ptr_hi;
    unsigned int data2ptr;
    unsigned int res1;
    unsigned int address_hi;
    unsigned int br_addr_hi;
    unsigned int res2[3];
};

/* Bits in control and status registers */
#define RUN	0x8000
#define PAUSE	0x4000
#define FLUSH	0x2000
#define WAKE	0x1000
#define DEAD	0x0800
#define ACTIVE	0x0400
#define BT	0x0100
#define DEVSTAT	0x00ff

/*
 * DBDMA command structure.  These fields are all little-endian!
 */
struct dbdma_cmd {
    unsigned short req_count;	/* requested byte transfer count */
    unsigned short command;	/* command word (has bit-fields) */
    unsigned int   phy_addr;	/* physical data address */
    unsigned int   cmd_dep;	/* command-dependent field */
    unsigned short res_count;	/* residual count after completion */
    unsigned short xfer_status;	/* transfer status */
};

/* DBDMA command values in command field */
#define OUTPUT_MORE	0	/* transfer memory data to stream */
#define OUTPUT_LAST	0x1000	/* ditto followed by end marker */
#define INPUT_MORE	0x2000	/* transfer stream data to memory */
#define INPUT_LAST	0x3000	/* ditto, expect end marker */
#define STORE_WORD	0x4000	/* write word (4 bytes) to device reg */
#define LOAD_WORD	0x5000	/* read word (4 bytes) from device reg */
#define DBDMA_NOP	0x6000	/* do nothing */
#define DBDMA_STOP	0x7000	/* suspend processing */

/* Key values in command field */
#define KEY_STREAM0	0	/* usual data stream */
#define KEY_STREAM1	0x100	/* control/status stream */
#define KEY_STREAM2	0x200	/* device-dependent stream */
#define KEY_STREAM3	0x300	/* device-dependent stream */
#define KEY_REGS	0x500	/* device register space */
#define KEY_SYSTEM	0x600	/* system memory-mapped space */
#define KEY_DEVICE	0x700	/* device memory-mapped space */

/* Interrupt control values in command field */
#define INTR_NEVER	0	/* don't interrupt */
#define INTR_IFSET	0x10	/* intr if condition bit is 1 */
#define INTR_IFCLR	0x20	/* intr if condition bit is 0 */
#define INTR_ALWAYS	0x30	/* always interrupt */

/* Branch control values in command field */
#define BR_NEVER	0	/* don't branch */
#define BR_IFSET	0x4	/* branch if condition bit is 1 */
#define BR_IFCLR	0x8	/* branch if condition bit is 0 */
#define BR_ALWAYS	0xc	/* always branch */

/* Wait control values in command field */
#define WAIT_NEVER	0	/* don't wait */
#define WAIT_IFSET	1	/* wait if condition bit is 1 */
#define WAIT_IFCLR	2	/* wait if condition bit is 0 */
#define WAIT_ALWAYS	3	/* always wait */

/* Align an address for a DBDMA command structure */
#define DBDMA_ALIGN(x)	(((unsigned long)(x) + sizeof(struct dbdma_cmd) - 1) \
			 & -sizeof(struct dbdma_cmd))

/* Useful macros */
#define DBDMA_DO_STOP(regs) do {				\
	out_le32(&((regs)->control), (RUN|FLUSH)<<16);		\
	while(in_le32(&((regs)->status)) & (ACTIVE|FLUSH))	\
		; \
} while(0)

#define DBDMA_DO_RESET(regs) do {				\
	out_le32(&((regs)->control), (ACTIVE|DEAD|WAKE|FLUSH|PAUSE|RUN)<<16);\
	while(in_le32(&((regs)->status)) & (RUN)) \
		; \
} while(0)

#endif /* _ASM_DBDMA_H_ */
#endif /* __KERNEL__ */
