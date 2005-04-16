#ifndef S390_CIO_IOASM_H
#define S390_CIO_IOASM_H

/*
 * TPI info structure
 */
struct tpi_info {
	__u32 reserved1	 : 16;	 /* reserved 0x00000001 */
	__u32 irq	 : 16;	 /* aka. subchannel number */
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

extern __inline__ int stsch(int irq, volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   stsch 0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int msch(int irq, volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   msch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int msch_err(int irq, volatile struct schib *addr)
{
	int ccode;

	__asm__ __volatile__(
		"    lhi  %0,%3\n"
		"    lr	  1,%1\n"
		"    msch 0(%2)\n"
		"0:  ipm  %0\n"
		"    srl  %0,28\n"
		"1:\n"
#ifdef CONFIG_ARCH_S390X
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
		: "d" (irq | 0x10000L), "a" (addr), "K" (-EIO)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int tsch(int irq, volatile struct irb *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   tsch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int tpi( volatile struct tpi_info *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   tpi	  0(%1)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int ssch(int irq, volatile struct orb *addr)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   ssch  0(%2)\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L), "a" (addr)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int rsch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   rsch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int csch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   csch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int hsch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   hsch\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int xsch(int irq)
{
	int ccode;

	__asm__ __volatile__(
		"   lr	  1,%1\n"
		"   .insn rre,0xb2760000,%1,0\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode)
		: "d" (irq | 0x10000L)
		: "cc", "1" );
	return ccode;
}

extern __inline__ int chsc(void *chsc_area)
{
	int cc;

	__asm__ __volatile__ (
		".insn	rre,0xb25f0000,%1,0	\n\t"
		"ipm	%0	\n\t"
		"srl	%0,28	\n\t"
		: "=d" (cc)
		: "d" (chsc_area)
		: "cc" );

	return cc;
}

extern __inline__ int iac( void)
{
	int ccode;

	__asm__ __volatile__(
		"   iac	  1\n"
		"   ipm	  %0\n"
		"   srl	  %0,28"
		: "=d" (ccode) : : "cc", "1" );
	return ccode;
}

extern __inline__ int rchp(int chpid)
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
