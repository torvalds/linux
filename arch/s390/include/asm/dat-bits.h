/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAT table and related structures
 *
 * Copyright IBM Corp. 2024
 *
 */

#ifndef _S390_DAT_BITS_H
#define _S390_DAT_BITS_H

union asce {
	unsigned long val;
	struct {
		unsigned long rsto: 52;/* Region- or Segment-Table Origin */
		unsigned long	  : 2;
		unsigned long g   : 1; /* Subspace Group control */
		unsigned long p   : 1; /* Private Space control */
		unsigned long s   : 1; /* Storage-Alteration-Event control */
		unsigned long x   : 1; /* Space-Switch-Event control */
		unsigned long r   : 1; /* Real-Space control */
		unsigned long	  : 1;
		unsigned long dt  : 2; /* Designation-Type control */
		unsigned long tl  : 2; /* Region- or Segment-Table Length */
	};
};

enum {
	ASCE_TYPE_SEGMENT = 0,
	ASCE_TYPE_REGION3 = 1,
	ASCE_TYPE_REGION2 = 2,
	ASCE_TYPE_REGION1 = 3
};

union region1_table_entry {
	unsigned long val;
	struct {
		unsigned long rto: 52;/* Region-Table Origin */
		unsigned long	 : 2;
		unsigned long p  : 1; /* DAT-Protection Bit */
		unsigned long	 : 1;
		unsigned long tf : 2; /* Region-Second-Table Offset */
		unsigned long i  : 1; /* Region-Invalid Bit */
		unsigned long	 : 1;
		unsigned long tt : 2; /* Table-Type Bits */
		unsigned long tl : 2; /* Region-Second-Table Length */
	};
};

union region2_table_entry {
	unsigned long val;
	struct {
		unsigned long rto: 52;/* Region-Table Origin */
		unsigned long	 : 2;
		unsigned long p  : 1; /* DAT-Protection Bit */
		unsigned long	 : 1;
		unsigned long tf : 2; /* Region-Third-Table Offset */
		unsigned long i  : 1; /* Region-Invalid Bit */
		unsigned long	 : 1;
		unsigned long tt : 2; /* Table-Type Bits */
		unsigned long tl : 2; /* Region-Third-Table Length */
	};
};

struct region3_table_entry_fc0 {
	unsigned long sto: 52;/* Segment-Table Origin */
	unsigned long	 : 1;
	unsigned long fc : 1; /* Format-Control */
	unsigned long p  : 1; /* DAT-Protection Bit */
	unsigned long	 : 1;
	unsigned long tf : 2; /* Segment-Table Offset */
	unsigned long i  : 1; /* Region-Invalid Bit */
	unsigned long cr : 1; /* Common-Region Bit */
	unsigned long tt : 2; /* Table-Type Bits */
	unsigned long tl : 2; /* Segment-Table Length */
};

struct region3_table_entry_fc1 {
	unsigned long rfaa: 33;/* Region-Frame Absolute Address */
	unsigned long	  : 14;
	unsigned long av  : 1; /* ACCF-Validity Control */
	unsigned long acc : 4; /* Access-Control Bits */
	unsigned long f   : 1; /* Fetch-Protection Bit */
	unsigned long fc  : 1; /* Format-Control */
	unsigned long p   : 1; /* DAT-Protection Bit */
	unsigned long iep : 1; /* Instruction-Execution-Protection */
	unsigned long	  : 2;
	unsigned long i   : 1; /* Region-Invalid Bit */
	unsigned long cr  : 1; /* Common-Region Bit */
	unsigned long tt  : 2; /* Table-Type Bits */
	unsigned long	  : 2;
};

union region3_table_entry {
	unsigned long val;
	struct region3_table_entry_fc0 fc0;
	struct region3_table_entry_fc1 fc1;
	struct {
		unsigned long	: 53;
		unsigned long fc: 1; /* Format-Control */
		unsigned long	: 4;
		unsigned long i : 1; /* Region-Invalid Bit */
		unsigned long cr: 1; /* Common-Region Bit */
		unsigned long tt: 2; /* Table-Type Bits */
		unsigned long	: 2;
	};
};

struct segment_table_entry_fc0 {
	unsigned long pto: 53;/* Page-Table Origin */
	unsigned long fc : 1; /* Format-Control */
	unsigned long p  : 1; /* DAT-Protection Bit */
	unsigned long	 : 3;
	unsigned long i  : 1; /* Segment-Invalid Bit */
	unsigned long cs : 1; /* Common-Segment Bit */
	unsigned long tt : 2; /* Table-Type Bits */
	unsigned long	 : 2;
};

struct segment_table_entry_fc1 {
	unsigned long sfaa: 44;/* Segment-Frame Absolute Address */
	unsigned long	  : 3;
	unsigned long av  : 1; /* ACCF-Validity Control */
	unsigned long acc : 4; /* Access-Control Bits */
	unsigned long f   : 1; /* Fetch-Protection Bit */
	unsigned long fc  : 1; /* Format-Control */
	unsigned long p   : 1; /* DAT-Protection Bit */
	unsigned long iep : 1; /* Instruction-Execution-Protection */
	unsigned long	  : 2;
	unsigned long i   : 1; /* Segment-Invalid Bit */
	unsigned long cs  : 1; /* Common-Segment Bit */
	unsigned long tt  : 2; /* Table-Type Bits */
	unsigned long	  : 2;
};

union segment_table_entry {
	unsigned long val;
	struct segment_table_entry_fc0 fc0;
	struct segment_table_entry_fc1 fc1;
	struct {
		unsigned long	: 53;
		unsigned long fc: 1; /* Format-Control */
		unsigned long	: 4;
		unsigned long i : 1; /* Segment-Invalid Bit */
		unsigned long cs: 1; /* Common-Segment Bit */
		unsigned long tt: 2; /* Table-Type Bits */
		unsigned long	: 2;
	};
};

union page_table_entry {
	unsigned long val;
	struct {
		unsigned long pfra: 52;/* Page-Frame Real Address */
		unsigned long z   : 1; /* Zero Bit */
		unsigned long i   : 1; /* Page-Invalid Bit */
		unsigned long p   : 1; /* DAT-Protection Bit */
		unsigned long iep : 1; /* Instruction-Execution-Protection */
		unsigned long	  : 8;
	};
};

enum {
	TABLE_TYPE_SEGMENT = 0,
	TABLE_TYPE_REGION3 = 1,
	TABLE_TYPE_REGION2 = 2,
	TABLE_TYPE_REGION1 = 3
};

#endif /* _S390_DAT_BITS_H */
