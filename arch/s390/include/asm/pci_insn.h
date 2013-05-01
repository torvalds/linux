#ifndef _ASM_S390_PCI_INSN_H
#define _ASM_S390_PCI_INSN_H

#include <linux/delay.h>

#define ZPCI_INSN_BUSY_DELAY	1	/* 1 microsecond */

/* Load/Store status codes */
#define ZPCI_PCI_ST_FUNC_NOT_ENABLED		4
#define ZPCI_PCI_ST_FUNC_IN_ERR			8
#define ZPCI_PCI_ST_BLOCKED			12
#define ZPCI_PCI_ST_INSUF_RES			16
#define ZPCI_PCI_ST_INVAL_AS			20
#define ZPCI_PCI_ST_FUNC_ALREADY_ENABLED	24
#define ZPCI_PCI_ST_DMA_AS_NOT_ENABLED		28
#define ZPCI_PCI_ST_2ND_OP_IN_INV_AS		36
#define ZPCI_PCI_ST_FUNC_NOT_AVAIL		40
#define ZPCI_PCI_ST_ALREADY_IN_RQ_STATE		44

/* Load/Store return codes */
#define ZPCI_PCI_LS_OK				0
#define ZPCI_PCI_LS_ERR				1
#define ZPCI_PCI_LS_BUSY			2
#define ZPCI_PCI_LS_INVAL_HANDLE		3

/* Load/Store address space identifiers */
#define ZPCI_PCIAS_MEMIO_0			0
#define ZPCI_PCIAS_MEMIO_1			1
#define ZPCI_PCIAS_MEMIO_2			2
#define ZPCI_PCIAS_MEMIO_3			3
#define ZPCI_PCIAS_MEMIO_4			4
#define ZPCI_PCIAS_MEMIO_5			5
#define ZPCI_PCIAS_CFGSPC			15

/* Modify PCI Function Controls */
#define ZPCI_MOD_FC_REG_INT	2
#define ZPCI_MOD_FC_DEREG_INT	3
#define ZPCI_MOD_FC_REG_IOAT	4
#define ZPCI_MOD_FC_DEREG_IOAT	5
#define ZPCI_MOD_FC_REREG_IOAT	6
#define ZPCI_MOD_FC_RESET_ERROR	7
#define ZPCI_MOD_FC_RESET_BLOCK	9
#define ZPCI_MOD_FC_SET_MEASURE	10

/* FIB function controls */
#define ZPCI_FIB_FC_ENABLED	0x80
#define ZPCI_FIB_FC_ERROR	0x40
#define ZPCI_FIB_FC_LS_BLOCKED	0x20
#define ZPCI_FIB_FC_DMAAS_REG	0x10

/* FIB function controls */
#define ZPCI_FIB_FC_ENABLED	0x80
#define ZPCI_FIB_FC_ERROR	0x40
#define ZPCI_FIB_FC_LS_BLOCKED	0x20
#define ZPCI_FIB_FC_DMAAS_REG	0x10

/* Function Information Block */
struct zpci_fib {
	u32 fmt		:  8;	/* format */
	u32		: 24;
	u32 reserved1;
	u8 fc;			/* function controls */
	u8 reserved2;
	u16 reserved3;
	u32 reserved4;
	u64 pba;		/* PCI base address */
	u64 pal;		/* PCI address limit */
	u64 iota;		/* I/O Translation Anchor */
	u32		:  1;
	u32 isc		:  3;	/* Interrupt subclass */
	u32 noi		: 12;	/* Number of interrupts */
	u32		:  2;
	u32 aibvo	:  6;	/* Adapter interrupt bit vector offset */
	u32 sum		:  1;	/* Adapter int summary bit enabled */
	u32		:  1;
	u32 aisbo	:  6;	/* Adapter int summary bit offset */
	u32 reserved5;
	u64 aibv;		/* Adapter int bit vector address */
	u64 aisb;		/* Adapter int summary bit address */
	u64 fmb_addr;		/* Function measurement block address and key */
	u64 reserved6;
	u64 reserved7;
} __packed;

/* Modify PCI Function Controls */
static inline u8 __mpcifc(u64 req, struct zpci_fib *fib, u8 *status)
{
	u8 cc;

	asm volatile (
		"	.insn	rxy,0xe300000000d0,%[req],%[fib]\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [req] "+d" (req), [fib] "+Q" (*fib)
		: : "cc");
	*status = req >> 24 & 0xff;
	return cc;
}

static inline int mpcifc_instr(u64 req, struct zpci_fib *fib)
{
	u8 cc, status;

	do {
		cc = __mpcifc(req, fib, &status);
		if (cc == 2)
			msleep(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		printk_once(KERN_ERR "%s: error cc: %d  status: %d\n",
			     __func__, cc, status);
	return (cc) ? -EIO : 0;
}

/* Refresh PCI Translations */
static inline u8 __rpcit(u64 fn, u64 addr, u64 range, u8 *status)
{
	register u64 __addr asm("2") = addr;
	register u64 __range asm("3") = range;
	u8 cc;

	asm volatile (
		"	.insn	rre,0xb9d30000,%[fn],%[addr]\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [fn] "+d" (fn)
		: [addr] "d" (__addr), "d" (__range)
		: "cc");
	*status = fn >> 24 & 0xff;
	return cc;
}

static inline int rpcit_instr(u64 fn, u64 addr, u64 range)
{
	u8 cc, status;

	do {
		cc = __rpcit(fn, addr, range, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		printk_once(KERN_ERR "%s: error cc: %d  status: %d  dma_addr: %Lx  size: %Lx\n",
			    __func__, cc, status, addr, range);
	return (cc) ? -EIO : 0;
}

/* Store PCI function controls */
static inline u8 __stpcifc(u32 handle, u8 space, struct zpci_fib *fib, u8 *status)
{
	u64 fn = (u64) handle << 32 | space << 16;
	u8 cc;

	asm volatile (
		"	.insn	rxy,0xe300000000d4,%[fn],%[fib]\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [fn] "+d" (fn), [fib] "=m" (*fib)
		: : "cc");
	*status = fn >> 24 & 0xff;
	return cc;
}

/* Set Interruption Controls */
static inline void sic_instr(u16 ctl, char *unused, u8 isc)
{
	asm volatile (
		"	.insn	rsy,0xeb00000000d1,%[ctl],%[isc],%[u]\n"
		: : [ctl] "d" (ctl), [isc] "d" (isc << 27), [u] "Q" (*unused));
}

/* PCI Load */
static inline u8 __pcilg(u64 *data, u64 req, u64 offset, u8 *status)
{
	register u64 __req asm("2") = req;
	register u64 __offset asm("3") = offset;
	u64 __data;
	u8 cc;

	asm volatile (
		"	.insn	rre,0xb9d20000,%[data],%[req]\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [data] "=d" (__data), [req] "+d" (__req)
		:  "d" (__offset)
		: "cc");
	*status = __req >> 24 & 0xff;
	*data = __data;
	return cc;
}

static inline int pcilg_instr(u64 *data, u64 req, u64 offset)
{
	u8 cc, status;

	do {
		cc = __pcilg(data, req, offset, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc) {
		printk_once(KERN_ERR "%s: error cc: %d  status: %d  req: %Lx  offset: %Lx\n",
			    __func__, cc, status, req, offset);
		/* TODO: on IO errors set data to 0xff...
		 * here or in users of pcilg (le conversion)?
		 */
	}
	return (cc) ? -EIO : 0;
}

/* PCI Store */
static inline u8 __pcistg(u64 data, u64 req, u64 offset, u8 *status)
{
	register u64 __req asm("2") = req;
	register u64 __offset asm("3") = offset;
	u8 cc;

	asm volatile (
		"	.insn	rre,0xb9d00000,%[data],%[req]\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [req] "+d" (__req)
		: "d" (__offset), [data] "d" (data)
		: "cc");
	*status = __req >> 24 & 0xff;
	return cc;
}

static inline int pcistg_instr(u64 data, u64 req, u64 offset)
{
	u8 cc, status;

	do {
		cc = __pcistg(data, req, offset, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		printk_once(KERN_ERR "%s: error cc: %d  status: %d  req: %Lx  offset: %Lx\n",
			__func__, cc, status, req, offset);
	return (cc) ? -EIO : 0;
}

/* PCI Store Block */
static inline u8 __pcistb(const u64 *data, u64 req, u64 offset, u8 *status)
{
	u8 cc;

	asm volatile (
		"	.insn	rsy,0xeb00000000d0,%[req],%[offset],%[data]\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [req] "+d" (req)
		: [offset] "d" (offset), [data] "Q" (*data)
		: "cc");
	*status = req >> 24 & 0xff;
	return cc;
}

static inline int pcistb_instr(const u64 *data, u64 req, u64 offset)
{
	u8 cc, status;

	do {
		cc = __pcistb(data, req, offset, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		printk_once(KERN_ERR "%s: error cc: %d  status: %d  req: %Lx  offset: %Lx\n",
			    __func__, cc, status, req, offset);
	return (cc) ? -EIO : 0;
}

#endif
