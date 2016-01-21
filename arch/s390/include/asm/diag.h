/*
 * s390 diagnose functions
 *
 * Copyright IBM Corp. 2007
 * Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _ASM_S390_DIAG_H
#define _ASM_S390_DIAG_H

#include <linux/percpu.h>

enum diag_stat_enum {
	DIAG_STAT_X008,
	DIAG_STAT_X00C,
	DIAG_STAT_X010,
	DIAG_STAT_X014,
	DIAG_STAT_X044,
	DIAG_STAT_X064,
	DIAG_STAT_X09C,
	DIAG_STAT_X0DC,
	DIAG_STAT_X204,
	DIAG_STAT_X210,
	DIAG_STAT_X224,
	DIAG_STAT_X250,
	DIAG_STAT_X258,
	DIAG_STAT_X288,
	DIAG_STAT_X2C4,
	DIAG_STAT_X2FC,
	DIAG_STAT_X304,
	DIAG_STAT_X308,
	DIAG_STAT_X500,
	NR_DIAG_STAT
};

void diag_stat_inc(enum diag_stat_enum nr);
void diag_stat_inc_norecursion(enum diag_stat_enum nr);

/*
 * Diagnose 10: Release page range
 */
static inline void diag10_range(unsigned long start_pfn, unsigned long num_pfn)
{
	unsigned long start_addr, end_addr;

	start_addr = start_pfn << PAGE_SHIFT;
	end_addr = (start_pfn + num_pfn - 1) << PAGE_SHIFT;

	diag_stat_inc(DIAG_STAT_X010);
	asm volatile(
		"0:	diag	%0,%1,0x10\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		EX_TABLE(1b, 1b)
		: : "a" (start_addr), "a" (end_addr));
}

/*
 * Diagnose 14: Input spool file manipulation
 */
extern int diag14(unsigned long rx, unsigned long ry1, unsigned long subcode);

/*
 * Diagnose 210: Get information about a virtual device
 */
struct diag210 {
	u16 vrdcdvno;	/* device number (input) */
	u16 vrdclen;	/* data block length (input) */
	u8 vrdcvcla;	/* virtual device class (output) */
	u8 vrdcvtyp;	/* virtual device type (output) */
	u8 vrdcvsta;	/* virtual device status (output) */
	u8 vrdcvfla;	/* virtual device flags (output) */
	u8 vrdcrccl;	/* real device class (output) */
	u8 vrdccrty;	/* real device type (output) */
	u8 vrdccrmd;	/* real device model (output) */
	u8 vrdccrft;	/* real device feature (output) */
} __attribute__((packed, aligned(4)));

extern int diag210(struct diag210 *addr);

#endif /* _ASM_S390_DIAG_H */
