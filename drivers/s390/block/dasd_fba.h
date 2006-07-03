/*
 * File...........: linux/drivers/s390/block/dasd_fba.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 */

#ifndef DASD_FBA_H
#define DASD_FBA_H

struct DE_fba_data {
	struct {
		unsigned char perm:2;	/* Permissions on this extent */
		unsigned char zero:2;	/* Must be zero */
		unsigned char da:1;	/* usually zero */
		unsigned char diag:1;	/* allow diagnose */
		unsigned char zero2:2;	/* zero */
	} __attribute__ ((packed)) mask;
	__u8 zero;		/* Must be zero */
	__u16 blk_size;		/* Blocksize */
	__u32 ext_loc;		/* Extent locator */
	__u32 ext_beg;		/* logical number of block 0 in extent */
	__u32 ext_end;		/* logocal number of last block in extent */
} __attribute__ ((packed));

struct LO_fba_data {
	struct {
		unsigned char zero:4;
		unsigned char cmd:4;
	} __attribute__ ((packed)) operation;
	__u8 auxiliary;
	__u16 blk_ct;
	__u32 blk_nr;
} __attribute__ ((packed));

struct dasd_fba_characteristics {
	union {
		__u8 c;
		struct {
			unsigned char reserved:1;
			unsigned char overrunnable:1;
			unsigned char burst_byte:1;
			unsigned char data_chain:1;
			unsigned char zeros:4;
		} __attribute__ ((packed)) bits;
	} __attribute__ ((packed)) mode;
	union {
		__u8 c;
		struct {
			unsigned char zero0:1;
			unsigned char removable:1;
			unsigned char shared:1;
			unsigned char zero1:1;
			unsigned char mam:1;
			unsigned char zeros:3;
		} __attribute__ ((packed)) bits;
	} __attribute__ ((packed)) features;
	__u8 dev_class;
	__u8 unit_type;
	__u16 blk_size;
	__u32 blk_per_cycl;
	__u32 blk_per_bound;
	__u32 blk_bdsa;
	__u32 reserved0;
	__u16 reserved1;
	__u16 blk_ce;
	__u32 reserved2;
	__u16 reserved3;
} __attribute__ ((packed));

#endif				/* DASD_FBA_H */
