// SPDX-License-Identifier: GPL-2.0
/*
 * Channel subsystem I/O instructions.
 */

#include <linux/export.h>

#include <asm/asm-extable.h>
#include <asm/chpid.h>
#include <asm/schid.h>
#include <asm/asm.h>
#include <asm/crw.h>

#include "ioasm.h"
#include "orb.h"
#include "cio.h"
#include "cio_inject.h"

static inline int __stsch(struct subchannel_id schid, struct schib *addr)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode, exception;

	exception = 1;
	asm_inline volatile(
		"	lgr	1,%[r1]\n"
		"	stsch	%[addr]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, ccode), [addr] "=Q" (*addr), [exc] "+d" (exception)
		: [r1] "d" (r1)
		: CC_CLOBBER_LIST("1"));
	return exception ? -EIO : CC_TRANSFORM(ccode);
}

int stsch(struct subchannel_id schid, struct schib *addr)
{
	int ccode;

	ccode = __stsch(schid, addr);
	trace_s390_cio_stsch(schid, addr, ccode);

	return ccode;
}
EXPORT_SYMBOL(stsch);

static inline int __msch(struct subchannel_id schid, struct schib *addr)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode, exception;

	exception = 1;
	asm_inline volatile(
		"	lgr	1,%[r1]\n"
		"	msch	%[addr]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, ccode), [exc] "+d" (exception)
		: [r1] "d" (r1), [addr] "Q" (*addr)
		: CC_CLOBBER_LIST("1"));
	return exception ? -EIO : CC_TRANSFORM(ccode);
}

int msch(struct subchannel_id schid, struct schib *addr)
{
	int ccode;

	ccode = __msch(schid, addr);
	trace_s390_cio_msch(schid, addr, ccode);

	return ccode;
}

static inline int __tsch(struct subchannel_id schid, struct irb *addr)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode;

	asm volatile(
		"	lgr	1,%[r1]\n"
		"	tsch	%[addr]\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode), [addr] "=Q" (*addr)
		: [r1] "d" (r1)
		: CC_CLOBBER_LIST("1"));
	return CC_TRANSFORM(ccode);
}

int tsch(struct subchannel_id schid, struct irb *addr)
{
	int ccode;

	ccode = __tsch(schid, addr);
	trace_s390_cio_tsch(schid, addr, ccode);

	return ccode;
}

static inline int __ssch(struct subchannel_id schid, union orb *addr)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode, exception;

	exception = 1;
	asm_inline volatile(
		"	lgr	1,%[r1]\n"
		"	ssch	%[addr]\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, ccode), [exc] "+d" (exception)
		: [r1] "d" (r1), [addr] "Q" (*addr)
		: CC_CLOBBER_LIST("memory", "1"));
	return CC_TRANSFORM(ccode);
}

int ssch(struct subchannel_id schid, union orb *addr)
{
	int ccode;

	ccode = __ssch(schid, addr);
	trace_s390_cio_ssch(schid, addr, ccode);

	return ccode;
}
EXPORT_SYMBOL(ssch);

static inline int __csch(struct subchannel_id schid)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode;

	asm volatile(
		"	lgr	1,%[r1]\n"
		"	csch\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode)
		: [r1] "d" (r1)
		: CC_CLOBBER_LIST("1"));
	return CC_TRANSFORM(ccode);
}

int csch(struct subchannel_id schid)
{
	int ccode;

	ccode = __csch(schid);
	trace_s390_cio_csch(schid, ccode);

	return ccode;
}
EXPORT_SYMBOL(csch);

int tpi(struct tpi_info *addr)
{
	int ccode;

	asm volatile(
		"	tpi	%[addr]\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode), [addr] "=Q" (*addr)
		:
		: CC_CLOBBER);
	ccode = CC_TRANSFORM(ccode);
	trace_s390_cio_tpi(addr, ccode);

	return ccode;
}

int chsc(void *chsc_area)
{
	typedef struct { char _[4096]; } addr_type;
	int cc, exception;

	exception = 1;
	asm_inline volatile(
		"	.insn	rre,0xb25f0000,%[chsc_area],0\n"
		"0:	lhi	%[exc],0\n"
		"1:\n"
		CC_IPM(cc)
		EX_TABLE(0b, 1b)
		: CC_OUT(cc, cc), "+m" (*(addr_type *)chsc_area), [exc] "+d" (exception)
		: [chsc_area] "d" (chsc_area)
		: CC_CLOBBER);
	cc = exception ? -EIO : CC_TRANSFORM(cc);
	trace_s390_cio_chsc(chsc_area, cc);

	return cc;
}
EXPORT_SYMBOL(chsc);

static inline int __rsch(struct subchannel_id schid)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode;

	asm volatile(
		"	lgr	1,%[r1]\n"
		"	rsch\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode)
		: [r1] "d" (r1)
		: CC_CLOBBER_LIST("memory", "1"));
	return CC_TRANSFORM(ccode);
}

int rsch(struct subchannel_id schid)
{
	int ccode;

	ccode = __rsch(schid);
	trace_s390_cio_rsch(schid, ccode);

	return ccode;
}

static inline int __hsch(struct subchannel_id schid)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode;

	asm volatile(
		"	lgr	1,%[r1]\n"
		"	hsch\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode)
		: [r1] "d" (r1)
		: CC_CLOBBER_LIST("1"));
	return CC_TRANSFORM(ccode);
}

int hsch(struct subchannel_id schid)
{
	int ccode;

	ccode = __hsch(schid);
	trace_s390_cio_hsch(schid, ccode);

	return ccode;
}
EXPORT_SYMBOL(hsch);

static inline int __xsch(struct subchannel_id schid)
{
	unsigned long r1 = *(unsigned int *)&schid;
	int ccode;

	asm volatile(
		"	lgr	1,%[r1]\n"
		"	xsch\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode)
		: [r1] "d" (r1)
		: CC_CLOBBER_LIST("1"));
	return CC_TRANSFORM(ccode);
}

int xsch(struct subchannel_id schid)
{
	int ccode;

	ccode = __xsch(schid);
	trace_s390_cio_xsch(schid, ccode);

	return ccode;
}

static inline int __stcrw(struct crw *crw)
{
	int ccode;

	asm volatile(
		"	stcrw	%[crw]\n"
		CC_IPM(cc)
		: CC_OUT(cc, ccode), [crw] "=Q" (*crw)
		:
		: CC_CLOBBER);
	return CC_TRANSFORM(ccode);
}

static inline int _stcrw(struct crw *crw)
{
#ifdef CONFIG_CIO_INJECT
	if (static_branch_unlikely(&cio_inject_enabled)) {
		if (stcrw_get_injected(crw) == 0)
			return 0;
	}
#endif

	return __stcrw(crw);
}

int stcrw(struct crw *crw)
{
	int ccode;

	ccode = _stcrw(crw);
	trace_s390_cio_stcrw(crw, ccode);

	return ccode;
}
