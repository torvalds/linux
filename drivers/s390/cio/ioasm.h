#ifndef S390_CIO_IOASM_H
#define S390_CIO_IOASM_H

#include "schid.h"

/*
 * TPI info structure
 */
struct tpi_info {
	struct subchannel_id schid;
	__u32 intparm;		 /* interruption parameter */
	__u32 adapter_IO : 1;
	__u32 reserved2	 : 1;
	__u32 isc	 : 3;
	__u32 reserved3	 : 12;
	__u32 int_type	 : 3;
	__u32 reserved4	 : 12;
} __attribute__ ((packed));


/*
 * Some S390 specific IO instructions as inline
 */

static inline int stsch(struct subchannel_id schid,
			    volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   stsch 0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid), "a" (addr), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int stsch_err(struct subchannel_id schid,
				volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"    lhi  %0,%3\n"
		"    lr	  1,%1\n"
		"    stsch 0(%2)\n"
		"0:  ipm  %0\n"
		"    srl  %0,28\n"
		"1:\n"
#ifdef CONFIG_64BIT
		".section __ex_table,\"a\"\n"
		"   .align 8\n"
		"   .quad 0b,1b\n"
		".previous"
#else
		".section __ex_table,\"a\"\n"
		"   .align 4\n"
		"   .long 0b,1b\n"
		".previous"
#endif
		: "=&d" (ccode)
		: "d" (schid), "a" (addr), "K" (-EIO), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int msch(struct subchannel_id schid,
			   volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   msch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid), "a" (addr), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int msch_err(struct subchannel_id schid,
			       volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"    lhi  %0,%3\n"
		"    lr	  1,%1\n"
		"    msch 0(%2)\n"
		"0:  ipm  %0\n"
		"    srl  %0,28\n"
		"1:\n"
#ifdef CONFIG_64BIT
		".section __ex_table,\"a\"\n"
		"   .align 8\n"
		"   .quad 0b,1b\n"
		".previous"
#else
		".section __ex_table,\"a\"\n"
		"   .align 4\n"
		"   .long 0b,1b\n"
		".previous"
#endif
		: "=&d" (ccode)
		: "d" (schid), "a" (addr), "K" (-EIO), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int tsch(struct subchannel_id schid,
			   volatile struct irb *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   tsch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid), "a" (addr), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int tpi( volatile struct tpi_info *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   tpi	  0(%1)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "a" (addr), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int ssch(struct subchannel_id schid,
			   volatile struct orb *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   ssch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid), "a" (addr), "m" (*addr)
		: "cc", "1" );
	return ccode;
}

static inline int rsch(struct subchannel_id schid)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   rsch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid)
		: "cc", "1" );
	return ccode;
}

static inline int csch(struct subchannel_id schid)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   csch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid)
		: "cc", "1" );
	return ccode;
}

static inline int hsch(struct subchannel_id schid)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   hsch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid)
		: "cc", "1" );
	return ccode;
}

static inline int xsch(struct subchannel_id schid)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   .insn rre,0xb2760000,%1,0\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (schid)
		: "cc", "1" );
	return ccode;
}

static inline int chsc(void *chsc_area)
{
	typedef struct { char _[4096]; } addr_type;
	int cc;

	__asm__ __volatile__ (
		".insn	rre,0xb25f0000,%2,0	\n\t"
		"ipm	%0	\n\t"
		"srl	%0,28	\n\t"
		: "=d" (cc), "=m" (*(addr_type *) chsc_area)
		: "d" (chsc_area), "m" (*(addr_type *) chsc_area)
		: "cc" );

	return cc;
}

static inline int iac( void)
{
	int ccode;

	__asm__ __volatile__(
		"   iac	  1\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode) : : "cc", "1" );
	return ccode;
}

static inline int rchp(int chpid)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   rchp\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (chpid)
		: "cc", "1" );
	return ccode;
}

#endif
