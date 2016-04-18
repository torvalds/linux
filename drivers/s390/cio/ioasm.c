/*
 * Channel subsystem I/O instructions.
 */

#include <linux/export.h>

#include <asm/chpid.h>
#include <asm/schid.h>
#include <asm/crw.h>

#include "ioasm.h"
#include "orb.h"
#include "cio.h"

int stsch(struct subchannel_id schid, struct schib *addr)
{
	register struct subchannel_id reg1 asm ("1") = schid;
	int ccode = -EIO;

	asm volatile(
		"	stsch	0(%3)\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (ccode), "=m" (*addr)
		: "d" (reg1), "a" (addr)
		: "cc");
	trace_s390_cio_stsch(schid, addr, ccode);

	return ccode;
}
EXPORT_SYMBOL(stsch);

int msch(struct subchannel_id schid, struct schib *addr)
{
	register struct subchannel_id reg1 asm ("1") = schid;
	int ccode = -EIO;

	asm volatile(
		"	msch	0(%2)\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (ccode)
		: "d" (reg1), "a" (addr), "m" (*addr)
		: "cc");
	trace_s390_cio_msch(schid, addr, ccode);

	return ccode;
}

int tsch(struct subchannel_id schid, struct irb *addr)
{
	register struct subchannel_id reg1 asm ("1") = schid;
	int ccode;

	asm volatile(
		"	tsch	0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode), "=m" (*addr)
		: "d" (reg1), "a" (addr)
		: "cc");
	trace_s390_cio_tsch(schid, addr, ccode);

	return ccode;
}

int ssch(struct subchannel_id schid, union orb *addr)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode = -EIO;

	asm volatile(
		"	ssch	0(%2)\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (ccode)
		: "d" (reg1), "a" (addr), "m" (*addr)
		: "cc", "memory");
	trace_s390_cio_ssch(schid, addr, ccode);

	return ccode;
}
EXPORT_SYMBOL(ssch);

int csch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	csch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc");
	trace_s390_cio_csch(schid, ccode);

	return ccode;
}
EXPORT_SYMBOL(csch);

int tpi(struct tpi_info *addr)
{
	int ccode;

	asm volatile(
		"	tpi	0(%2)\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode), "=m" (*addr)
		: "a" (addr)
		: "cc");
	trace_s390_cio_tpi(addr, ccode);

	return ccode;
}

int chsc(void *chsc_area)
{
	typedef struct { char _[4096]; } addr_type;
	int cc;

	asm volatile(
		"	.insn	rre,0xb25f0000,%2,0\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc), "=m" (*(addr_type *) chsc_area)
		: "d" (chsc_area), "m" (*(addr_type *) chsc_area)
		: "cc");
	trace_s390_cio_chsc(chsc_area, cc);

	return cc;
}
EXPORT_SYMBOL(chsc);

int rchp(struct chp_id chpid)
{
	register struct chp_id reg1 asm ("1") = chpid;
	int ccode;

	asm volatile(
		"	lr	1,%1\n"
		"	rchp\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode) : "d" (reg1) : "cc");
	trace_s390_cio_rchp(chpid, ccode);

	return ccode;
}

int rsch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	rsch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc", "memory");
	trace_s390_cio_rsch(schid, ccode);

	return ccode;
}

int hsch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	hsch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc");
	trace_s390_cio_hsch(schid, ccode);

	return ccode;
}

int xsch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	xsch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc");
	trace_s390_cio_xsch(schid, ccode);

	return ccode;
}

int stcrw(struct crw *crw)
{
	int ccode;

	asm volatile(
		"	stcrw	0(%2)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode), "=m" (*crw)
		: "a" (crw)
		: "cc");
	trace_s390_cio_stcrw(crw, ccode);

	return ccode;
}
