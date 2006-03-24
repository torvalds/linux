/*
 *  drivers/s390/char/tape_3590.h
 *    tape device discipline for 3590 tapes.
 *
 *    Copyright (C) IBM Corp. 2001,2006
 *    Author(s): Stefan Bader <shbader@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _TAPE_3590_H
#define _TAPE_3590_H

#define MEDIUM_SENSE	0xc2
#define READ_PREVIOUS	0x0a
#define MODE_SENSE	0xcf
#define PERFORM_SS_FUNC 0x77
#define READ_SS_DATA	0x3e

#define PREP_RD_SS_DATA 0x18
#define RD_ATTMSG	0x3

#define SENSE_BRA_PER  0
#define SENSE_BRA_CONT 1
#define SENSE_BRA_RE   2
#define SENSE_BRA_DRE  3

#define SENSE_FMT_LIBRARY	0x23
#define SENSE_FMT_UNSOLICITED	0x40
#define SENSE_FMT_COMMAND_REJ	0x41
#define SENSE_FMT_COMMAND_EXEC0 0x50
#define SENSE_FMT_COMMAND_EXEC1 0x51
#define SENSE_FMT_EVENT0	0x60
#define SENSE_FMT_EVENT1	0x61
#define SENSE_FMT_MIM		0x70
#define SENSE_FMT_SIM		0x71

#define MSENSE_UNASSOCIATED	 0x00
#define MSENSE_ASSOCIATED_MOUNT	 0x01
#define MSENSE_ASSOCIATED_UMOUNT 0x02

#define TAPE_3590_MAX_MSG	 0xb0

/* Datatypes */

struct tape_3590_disc_data {
	unsigned char modeset_byte;
	int read_back_op;
};

struct tape_3590_sense {

	unsigned int command_rej:1;
	unsigned int interv_req:1;
	unsigned int bus_out_check:1;
	unsigned int eq_check:1;
	unsigned int data_check:1;
	unsigned int overrun:1;
	unsigned int def_unit_check:1;
	unsigned int assgnd_elsew:1;

	unsigned int locate_fail:1;
	unsigned int inst_online:1;
	unsigned int reserved:1;
	unsigned int blk_seq_err:1;
	unsigned int begin_part:1;
	unsigned int wr_mode:1;
	unsigned int wr_prot:1;
	unsigned int not_cap:1;

	unsigned int bra:2;
	unsigned int lc:3;
	unsigned int vlf_active:1;
	unsigned int stm:1;
	unsigned int med_pos:1;

	unsigned int rac:8;

	unsigned int rc_rqc:16;

	unsigned int mc:8;

	unsigned int sense_fmt:8;

	union {
		struct {
			unsigned int emc:4;
			unsigned int smc:4;
			unsigned int sev:2;
			unsigned int reserved:6;
			unsigned int md:8;
			unsigned int refcode:8;
			unsigned int mid:16;
			unsigned int mp:16;
			unsigned char volid[6];
			unsigned int fid:8;
		} f70;
		struct {
			unsigned int emc:4;
			unsigned int smc:4;
			unsigned int sev:2;
			unsigned int reserved1:5;
			unsigned int mdf:1;
			unsigned char md[3];
			unsigned int simid:8;
			unsigned int uid:16;
			unsigned int refcode1:16;
			unsigned int refcode2:16;
			unsigned int refcode3:16;
			unsigned int reserved2:8;
		} f71;
		unsigned char data[14];
	} fmt;
	unsigned char pad[10];

} __attribute__ ((packed));

struct tape_3590_med_sense {
	unsigned int macst:4;
	unsigned int masst:4;
	char pad[127];
} __attribute__ ((packed));

#endif /* _TAPE_3590_H */
