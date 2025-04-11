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
#include <asm/asm-extable.h>
#include <asm/facility.h>
#include <asm/pci_insn.h>
#include <asm/pci_debug.h>
#include <asm/pci_io.h>
#include <asm/processor.h>
#include <asm/asm.h>

#define ZPCI_INSN_BUSY_DELAY	1	/* 1 microsecond */

struct zpci_err_insn_data {
	u8 insn;
	u8 cc;
	u8 status;
	union {
		struct {
			u64 req;
			u64 offset;
		};
		struct {
			u64 addr;
			u64 len;
		};
	};
} __packed;

static inline void zpci_err_insn_req(int lvl, u8 insn, u8 cc, u8 status,
				     u64 req, u64 offset)
{
	struct zpci_err_insn_data data = {
		.insn = insn, .cc = cc, .status = status,
		.req = req, .offset = offset};

	zpci_err_hex_level(lvl, &data, sizeof(data));
}

static inline void zpci_err_insn_addr(int lvl, u8 insn, u8 cc, u8 status,
				      u64 addr, u64 len)
{
	struct zpci_err_insn_data data = {
		.insn = insn, .cc = cc, .status = status,
		.addr = addr, .len = len};

	zpci_err_hex_level(lvl, &data, sizeof(data));
}

/* Modify PCI Function Controls */
static inline u8 __mpcifc(u64 req, struct zpci_fib *fib, u8 *status)
{
	int cc;

	asm volatile (
		"	.insn	rxy,0xe300000000d0,%[req],%[fib]\n"
		CC_IPM(cc)
		: CC_OUT(cc, cc), [req] "+d" (req), [fib] "+Q" (*fib)
		:
		: CC_CLOBBER);
	*status = req >> 24 & 0xff;
	return CC_TRANSFORM(cc);
}

u8 zpci_mod_fc(u64 req, struct zpci_fib *fib, u8 *status)
{
	bool retried = false;
	u8 cc;

	do {
		cc = __mpcifc(req, fib, status);
		if (cc == 2) {
			msleep(ZPCI_INSN_BUSY_DELAY);
			if (!retried) {
				zpci_err_insn_req(1, 'M', cc, *status, req, 0);
				retried = true;
			}
		}
	} while (cc == 2);

	if (cc)
		zpci_err_insn_req(0, 'M', cc, *status, req, 0);
	else if (retried)
		zpci_err_insn_req(1, 'M', cc, *status, req, 0);

	return cc;
}
EXPORT_SYMBOL_GPL(zpci_mod_fc);

/* Refresh PCI Translations */
static inline u8 __rpcit(u64 fn, u64 addr, u64 range, u8 *status)
{
	union register_pair addr_range = {.even = addr, .odd = range};
	int cc;

	asm volatile (
		"	.insn	rre,0xb9d30000,%[fn],%[addr_range]\n"
		CC_IPM(cc)
		: CC_OUT(cc, cc), [fn] "+d" (fn)
		: [addr_range] "d" (addr_range.pair)
		: CC_CLOBBER);
	*status = fn >> 24 & 0xff;
	return CC_TRANSFORM(cc);
}

int zpci_refresh_trans(u64 fn, u64 addr, u64 range)
{
	bool retried = false;
	u8 cc, status;

	do {
		cc = __rpcit(fn, addr, range, &status);
		if (cc == 2) {
			udelay(ZPCI_INSN_BUSY_DELAY);
			if (!retried) {
				zpci_err_insn_addr(1, 'R', cc, status, addr, range);
				retried = true;
			}
		}
	} while (cc == 2);

	if (cc)
		zpci_err_insn_addr(0, 'R', cc, status, addr, range);
	else if (retried)
		zpci_err_insn_addr(1, 'R', cc, status, addr, range);

	if (cc == 1 && (status == 4 || status == 16))
		return -ENOMEM;

	return (cc) ? -EIO : 0;
}

/* Set Interruption Controls */
int zpci_set_irq_ctrl(u16 ctl, u8 isc, union zpci_sic_iib *iib)
{
	if (!test_facility(72))
		return -EIO;

	asm volatile(
		".insn	rsy,0xeb00000000d1,%[ctl],%[isc],%[iib]\n"
		: : [ctl] "d" (ctl), [isc] "d" (isc << 27), [iib] "Q" (*iib));

	return 0;
}
EXPORT_SYMBOL_GPL(zpci_set_irq_ctrl);

/* PCI Load */
static inline int ____pcilg(u64 *data, u64 req, u64 offset, u8 *status)
{
	union register_pair req_off = {.even = req, .odd = offset};
	int cc, exception;
	u64 __data;

	exception = 1;
	asm_inline volatile (
		"	.insn	rre,0xb9d20000,%[data],%[req_off]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), [data] "=d" (__data),
		  [req_off] "+d" (req_off.pair), [exc] "+d" (exception)
		:
		: CC_CLOBBER);
	*status = req_off.even >> 24 & 0xff;
	*data = __data;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
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
	bool retried = false;
	u8 status;
	int cc;

	do {
		cc = __pcilg(data, req, offset, &status);
		if (cc == 2) {
			udelay(ZPCI_INSN_BUSY_DELAY);
			if (!retried) {
				zpci_err_insn_req(1, 'l', cc, status, req, offset);
				retried = true;
			}
		}
	} while (cc == 2);

	if (cc)
		zpci_err_insn_req(0, 'l', cc, status, req, offset);
	else if (retried)
		zpci_err_insn_req(1, 'l', cc, status, req, offset);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(__zpci_load);

static inline int zpci_load_fh(u64 *data, const volatile void __iomem *addr,
			       unsigned long len)
{
	struct zpci_iomap_entry *entry = &zpci_iomap_start[ZPCI_IDX(addr)];
	u64 req = ZPCI_CREATE_REQ(READ_ONCE(entry->fh), entry->bar, len);

	return __zpci_load(data, req, ZPCI_OFFSET(addr));
}

static inline int __pcilg_mio(u64 *data, u64 ioaddr, u64 len, u8 *status)
{
	union register_pair ioaddr_len = {.even = ioaddr, .odd = len};
	int cc, exception;
	u64 __data;

	exception = 1;
	asm_inline volatile (
		"       .insn   rre,0xb9d60000,%[data],%[ioaddr_len]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), [data] "=d" (__data),
		  [ioaddr_len] "+d" (ioaddr_len.pair), [exc] "+d" (exception)
		:
		: CC_CLOBBER);
	*status = ioaddr_len.odd >> 24 & 0xff;
	*data = __data;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
}

int zpci_load(u64 *data, const volatile void __iomem *addr, unsigned long len)
{
	u8 status;
	int cc;

	if (!static_branch_unlikely(&have_mio))
		return zpci_load_fh(data, addr, len);

	cc = __pcilg_mio(data, (__force u64) addr, len, &status);
	if (cc)
		zpci_err_insn_addr(0, 'L', cc, status, (__force u64) addr, len);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(zpci_load);

/* PCI Store */
static inline int __pcistg(u64 data, u64 req, u64 offset, u8 *status)
{
	union register_pair req_off = {.even = req, .odd = offset};
	int cc, exception;

	exception = 1;
	asm_inline volatile (
		"	.insn	rre,0xb9d00000,%[data],%[req_off]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), [req_off] "+d" (req_off.pair), [exc] "+d" (exception)
		: [data] "d" (data)
		: CC_CLOBBER);
	*status = req_off.even >> 24 & 0xff;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
}

int __zpci_store(u64 data, u64 req, u64 offset)
{
	bool retried = false;
	u8 status;
	int cc;

	do {
		cc = __pcistg(data, req, offset, &status);
		if (cc == 2) {
			udelay(ZPCI_INSN_BUSY_DELAY);
			if (!retried) {
				zpci_err_insn_req(1, 's', cc, status, req, offset);
				retried = true;
			}
		}
	} while (cc == 2);

	if (cc)
		zpci_err_insn_req(0, 's', cc, status, req, offset);
	else if (retried)
		zpci_err_insn_req(1, 's', cc, status, req, offset);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(__zpci_store);

static inline int zpci_store_fh(const volatile void __iomem *addr, u64 data,
				unsigned long len)
{
	struct zpci_iomap_entry *entry = &zpci_iomap_start[ZPCI_IDX(addr)];
	u64 req = ZPCI_CREATE_REQ(READ_ONCE(entry->fh), entry->bar, len);

	return __zpci_store(data, req, ZPCI_OFFSET(addr));
}

static inline int __pcistg_mio(u64 data, u64 ioaddr, u64 len, u8 *status)
{
	union register_pair ioaddr_len = {.even = ioaddr, .odd = len};
	int cc, exception;

	exception = 1;
	asm_inline volatile (
		"       .insn   rre,0xb9d40000,%[data],%[ioaddr_len]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), [ioaddr_len] "+d" (ioaddr_len.pair), [exc] "+d" (exception)
		: [data] "d" (data)
		: CC_CLOBBER_LIST("memory"));
	*status = ioaddr_len.odd >> 24 & 0xff;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
}

int zpci_store(const volatile void __iomem *addr, u64 data, unsigned long len)
{
	u8 status;
	int cc;

	if (!static_branch_unlikely(&have_mio))
		return zpci_store_fh(addr, data, len);

	cc = __pcistg_mio(data, (__force u64) addr, len, &status);
	if (cc)
		zpci_err_insn_addr(0, 'S', cc, status, (__force u64) addr, len);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(zpci_store);

/* PCI Store Block */
static inline int __pcistb(const u64 *data, u64 req, u64 offset, u8 *status)
{
	int cc, exception;

	exception = 1;
	asm_inline volatile (
		"	.insn	rsy,0xeb00000000d0,%[req],%[offset],%[data]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), [req] "+d" (req), [exc] "+d" (exception)
		: [offset] "d" (offset), [data] "Q" (*data)
		: CC_CLOBBER);
	*status = req >> 24 & 0xff;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
}

int __zpci_store_block(const u64 *data, u64 req, u64 offset)
{
	bool retried = false;
	u8 status;
	int cc;

	do {
		cc = __pcistb(data, req, offset, &status);
		if (cc == 2) {
			udelay(ZPCI_INSN_BUSY_DELAY);
			if (!retried) {
				zpci_err_insn_req(0, 'b', cc, status, req, offset);
				retried = true;
			}
		}
	} while (cc == 2);

	if (cc)
		zpci_err_insn_req(0, 'b', cc, status, req, offset);
	else if (retried)
		zpci_err_insn_req(1, 'b', cc, status, req, offset);

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
	int cc, exception;

	exception = 1;
	asm_inline volatile (
		"       .insn   rsy,0xeb00000000d4,%[len],%[ioaddr],%[data]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), [len] "+d" (len), [exc] "+d" (exception)
		: [ioaddr] "d" (ioaddr), [data] "Q" (*data)
		: CC_CLOBBER);
	*status = len >> 24 & 0xff;
	return exception ? -ENXIO : CC_TRANSFORM(cc);
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
		zpci_err_insn_addr(0, 'B', cc, status, (__force u64) dst, len);

	return (cc > 0) ? -EIO : cc;
}
EXPORT_SYMBOL_GPL(zpci_write_block);

static inline void __pciwb_mio(void)
{
	asm volatile (".insn    rre,0xb9d50000,0,0\n");
}

void zpci_barrier(void)
{
	if (static_branch_likely(&have_mio))
		__pciwb_mio();
}
EXPORT_SYMBOL_GPL(zpci_barrier);
