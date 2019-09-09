// SPDX-License-Identifier: GPL-2.0
/*
 * s390 specific pci instructions
 *
 * Copyright IBM Corp. 2013
 */

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/jump_label.h>
#include <asm/facility.h>
#include <asm/pci_insn.h>
#include <asm/pci_debug.h>
#include <asm/pci_io.h>
#include <asm/processor.h>

#define ZPCI_INSN_BUSY_DELAY	1	/* 1 microsecond */

static inline void zpci_err_insn(u8 cc, u8 status, u64 req, u64 offset)
{
	struct {
		u64 req;
		u64 offset;
		u8 cc;
		u8 status;
	} __packed data = {req, offset, cc, status};

	zpci_err_hex(&data, sizeof(data));
}

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

u8 zpci_mod_fc(u64 req, struct zpci_fib *fib, u8 *status)
{
	u8 cc;

	do {
		cc = __mpcifc(req, fib, status);
		if (cc == 2)
			msleep(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		zpci_err_insn(cc, *status, req, 0);

	return cc;
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

int zpci_refresh_trans(u64 fn, u64 addr, u64 range)
{
	u8 cc, status;

	do {
		cc = __rpcit(fn, addr, range, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		zpci_err_insn(cc, status, addr, range);

	if (cc == 1 && (status == 4 || status == 16))
		return -ENOMEM;

	return (cc) ? -EIO : 0;
}

/* Set Interruption Controls */
int __zpci_set_irq_ctrl(u16 ctl, u8 isc, union zpci_sic_iib *iib)
{
	if (!test_facility(72))
		return -EIO;

	asm volatile(
		".insn	rsy,0xeb00000000d1,%[ctl],%[isc],%[iib]\n"
		: : [ctl] "d" (ctl), [isc] "d" (isc << 27), [iib] "Q" (*iib));

	return 0;
}

/* PCI Load */
static inline int ____pcilg(u64 *data, u64 req, u64 offset, u8 *status)
{
	register u64 __req asm("2") = req;
	register u64 __offset asm("3") = offset;
	int cc = -ENXIO;
	u64 __data;

	asm volatile (
		"	.insn	rre,0xb9d20000,%[data],%[req]\n"
		"0:	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [data] "=d" (__data), [req] "+d" (__req)
		:  "d" (__offset)
		: "cc");
	*status = __req >> 24 & 0xff;
	*data = __data;
	return cc;
}

static inline int __pcilg(u64 *data, u64 req, u64 offset, u8 *status)
{
	u64 __data;
	int cc;

	cc = ____pcilg(&__data, req, offset, status);
	if (!cc)
		*data = __data;

	return cc;
}

int __zpci_load(u64 *data, u64 req, u64 offset)
{
	u8 status;
	int cc;

	do {
		cc = __pcilg(data, req, offset, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		zpci_err_insn(cc, status, req, offset);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(__zpci_load);

static inline int zpci_load_fh(u64 *data, const volatile void __iomem *addr,
			       unsigned long len)
{
	struct zpci_iomap_entry *entry = &zpci_iomap_start[ZPCI_IDX(addr)];
	u64 req = ZPCI_CREATE_REQ(entry->fh, entry->bar, len);

	return __zpci_load(data, req, ZPCI_OFFSET(addr));
}

static inline int __pcilg_mio(u64 *data, u64 ioaddr, u64 len, u8 *status)
{
	register u64 addr asm("2") = ioaddr;
	register u64 r3 asm("3") = len;
	int cc = -ENXIO;
	u64 __data;

	asm volatile (
		"       .insn   rre,0xb9d60000,%[data],%[ioaddr]\n"
		"0:     ipm     %[cc]\n"
		"       srl     %[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [data] "=d" (__data), "+d" (r3)
		: [ioaddr] "d" (addr)
		: "cc");
	*status = r3 >> 24 & 0xff;
	*data = __data;
	return cc;
}

int zpci_load(u64 *data, const volatile void __iomem *addr, unsigned long len)
{
	u8 status;
	int cc;

	if (!static_branch_unlikely(&have_mio))
		return zpci_load_fh(data, addr, len);

	cc = __pcilg_mio(data, (__force u64) addr, len, &status);
	if (cc)
		zpci_err_insn(cc, status, 0, (__force u64) addr);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(zpci_load);

/* PCI Store */
static inline int __pcistg(u64 data, u64 req, u64 offset, u8 *status)
{
	register u64 __req asm("2") = req;
	register u64 __offset asm("3") = offset;
	int cc = -ENXIO;

	asm volatile (
		"	.insn	rre,0xb9d00000,%[data],%[req]\n"
		"0:	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [req] "+d" (__req)
		: "d" (__offset), [data] "d" (data)
		: "cc");
	*status = __req >> 24 & 0xff;
	return cc;
}

int __zpci_store(u64 data, u64 req, u64 offset)
{
	u8 status;
	int cc;

	do {
		cc = __pcistg(data, req, offset, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		zpci_err_insn(cc, status, req, offset);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(__zpci_store);

static inline int zpci_store_fh(const volatile void __iomem *addr, u64 data,
				unsigned long len)
{
	struct zpci_iomap_entry *entry = &zpci_iomap_start[ZPCI_IDX(addr)];
	u64 req = ZPCI_CREATE_REQ(entry->fh, entry->bar, len);

	return __zpci_store(data, req, ZPCI_OFFSET(addr));
}

static inline int __pcistg_mio(u64 data, u64 ioaddr, u64 len, u8 *status)
{
	register u64 addr asm("2") = ioaddr;
	register u64 r3 asm("3") = len;
	int cc = -ENXIO;

	asm volatile (
		"       .insn   rre,0xb9d40000,%[data],%[ioaddr]\n"
		"0:     ipm     %[cc]\n"
		"       srl     %[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), "+d" (r3)
		: [data] "d" (data), [ioaddr] "d" (addr)
		: "cc");
	*status = r3 >> 24 & 0xff;
	return cc;
}

int zpci_store(const volatile void __iomem *addr, u64 data, unsigned long len)
{
	u8 status;
	int cc;

	if (!static_branch_unlikely(&have_mio))
		return zpci_store_fh(addr, data, len);

	cc = __pcistg_mio(data, (__force u64) addr, len, &status);
	if (cc)
		zpci_err_insn(cc, status, 0, (__force u64) addr);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(zpci_store);

/* PCI Store Block */
static inline int __pcistb(const u64 *data, u64 req, u64 offset, u8 *status)
{
	int cc = -ENXIO;

	asm volatile (
		"	.insn	rsy,0xeb00000000d0,%[req],%[offset],%[data]\n"
		"0:	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [req] "+d" (req)
		: [offset] "d" (offset), [data] "Q" (*data)
		: "cc");
	*status = req >> 24 & 0xff;
	return cc;
}

int __zpci_store_block(const u64 *data, u64 req, u64 offset)
{
	u8 status;
	int cc;

	do {
		cc = __pcistb(data, req, offset, &status);
		if (cc == 2)
			udelay(ZPCI_INSN_BUSY_DELAY);
	} while (cc == 2);

	if (cc)
		zpci_err_insn(cc, status, req, offset);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(__zpci_store_block);

static inline int zpci_write_block_fh(volatile void __iomem *dst,
				      const void *src, unsigned long len)
{
	struct zpci_iomap_entry *entry = &zpci_iomap_start[ZPCI_IDX(dst)];
	u64 req = ZPCI_CREATE_REQ(entry->fh, entry->bar, len);
	u64 offset = ZPCI_OFFSET(dst);

	return __zpci_store_block(src, req, offset);
}

static inline int __pcistb_mio(const u64 *data, u64 ioaddr, u64 len, u8 *status)
{
	int cc = -ENXIO;

	asm volatile (
		"       .insn   rsy,0xeb00000000d4,%[len],%[ioaddr],%[data]\n"
		"0:     ipm     %[cc]\n"
		"       srl     %[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [len] "+d" (len)
		: [ioaddr] "d" (ioaddr), [data] "Q" (*data)
		: "cc");
	*status = len >> 24 & 0xff;
	return cc;
}

int zpci_write_block(volatile void __iomem *dst,
		     const void *src, unsigned long len)
{
	u8 status;
	int cc;

	if (!static_branch_unlikely(&have_mio))
		return zpci_write_block_fh(dst, src, len);

	cc = __pcistb_mio(src, (__force u64) dst, len, &status);
	if (cc)
		zpci_err_insn(cc, status, 0, (__force u64) dst);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(zpci_write_block);

static inline void __pciwb_mio(void)
{
	unsigned long unused = 0;

	asm volatile (".insn    rre,0xb9d50000,%[op],%[op]\n"
		      : [op] "+d" (unused));
}

void zpci_barrier(void)
{
	if (static_branch_likely(&have_mio))
		__pciwb_mio();
}
EXPORT_SYMBOL_GPL(zpci_barrier);
