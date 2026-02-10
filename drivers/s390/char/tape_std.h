/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    standard tape device functions for ibm tapes.
 *
 *    Copyright IBM Corp. 2001, 2006
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _TAPE_STD_H
#define _TAPE_STD_H

/*
 * Biggest block size of 256K to handle.
 */
#define MAX_BLOCKSIZE	262144

/*
 * The CCW commands for the Tape type of command.
 */
#define BACKSPACEBLOCK		0x27	/* Back Space block */
#define BACKSPACEFILE		0x2f	/* Back Space file */
#define DATA_SEC_ERASE		0x97	/* Data security erase */
#define ERASE_GAP		0x17	/* Erase Gap */
#define FORSPACEBLOCK		0x37	/* Forward space block */
#define FORSPACEFILE		0x3F	/* Forward Space file */
#define NOP			0x03	/* No operation	*/
#define READ_FORWARD		0x02	/* Read forward */
#define REWIND			0x07	/* Rewind */
#define REWIND_UNLOAD		0x0F	/* Rewind and Unload */
#define SENSE			0x04	/* Sense */
#define WRITE_CMD		0x01	/* Write */
#define WRITETAPEMARK		0x1F	/* Write Tape Mark */

#define ASSIGN			0xB7	/* Assign */
#define LOCATE			0x4F	/* Locate Block */
#define MODE_SET_DB		0xDB	/* Mode Set */
#define READ_BLOCK_ID		0x22	/* Read Block ID */
#define UNASSIGN		0xC7	/* Unassign */

#define SENSE_COMMAND_REJECT		0x80
#define SENSE_INTERVENTION_REQUIRED	0x40
#define SENSE_BUS_OUT_CHECK		0x20
#define SENSE_EQUIPMENT_CHECK		0x10
#define SENSE_DATA_CHECK		0x08
#define SENSE_OVERRUN			0x04
#define SENSE_DEFERRED_UNIT_CHECK	0x02
#define SENSE_ASSIGNED_ELSEWHERE	0x01

#define SENSE_LOCATE_FAILURE		0x80
#define SENSE_DRIVE_ONLINE		0x40
#define SENSE_RESERVED			0x20
#define SENSE_RECORD_SEQUENCE_ERR	0x10
#define SENSE_BEGINNING_OF_TAPE		0x08
#define SENSE_WRITE_MODE		0x04
#define SENSE_WRITE_PROTECT		0x02
#define SENSE_NOT_CAPABLE		0x01

#define SENSE_CHANNEL_ADAPTER_CODE	0xE0
#define SENSE_CHANNEL_ADAPTER_LOC	0x10
#define SENSE_REPORTING_CU		0x08
#define SENSE_AUTOMATIC_LOADER		0x04
#define SENSE_TAPE_SYNC_MODE		0x02
#define SENSE_TAPE_POSITIONING		0x01

/* discipline functions */
struct tape_request *tape_std_read_block(struct tape_device *);
void tape_std_read_backward(struct tape_device *device,
			    struct tape_request *request);
struct tape_request *tape_std_write_block(struct tape_device *);

/* Some non-mtop commands. */
int tape_std_assign(struct tape_device *);
int tape_std_unassign(struct tape_device *);
int tape_std_read_block_id(struct tape_device *device, __u64 *id);
int tape_std_terminate_write(struct tape_device *);

/* Standard magnetic tape commands. */
int tape_std_mtbsf(struct tape_device *, int);
int tape_std_mtbsfm(struct tape_device *, int);
int tape_std_mtbsr(struct tape_device *, int);
int tape_std_mtcompression(struct tape_device *, int);
int tape_std_mteom(struct tape_device *, int);
int tape_std_mterase(struct tape_device *, int);
int tape_std_mtfsf(struct tape_device *, int);
int tape_std_mtfsfm(struct tape_device *, int);
int tape_std_mtfsr(struct tape_device *, int);
int tape_std_mtload(struct tape_device *, int);
int tape_std_mtnop(struct tape_device *, int);
int tape_std_mtoffl(struct tape_device *, int);
int tape_std_mtreset(struct tape_device *, int);
int tape_std_mtreten(struct tape_device *, int);
int tape_std_mtrew(struct tape_device *, int);
int tape_std_mtsetblk(struct tape_device *, int);
int tape_std_mtunload(struct tape_device *, int);
int tape_std_mtweof(struct tape_device *, int);

/* Event handlers */
void tape_std_process_eov(struct tape_device *);

/* S390 tape types */
enum s390_tape_type {
        tape_3490,
};

#endif // _TAPE_STD_H
