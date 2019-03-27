/* Definitions and macros for support of AMD's remote debugger, MiniMON.
   Copyright 1990, 1991 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/*
 * Some basic types.  FIXME, this should be done by declaring bitfield
 * sizes in the structs.  We can't portably depend on a "long int" being
 * 32 bits, etc.
 */
typedef long int INT32;		/* 32 bit integer */
typedef unsigned long int UINT32;	/* 32 bit integer (unsigned) */
typedef unsigned long int ADDR32;	/* 32 bit address */
typedef unsigned long int INST32;	/* 32 bit instruction */
typedef long int BOOLEAN;	/* Boolean value (32 bit) */
typedef unsigned char BYTE;	/* byte (8 bit) */
typedef short int INT16;	/* 16 bit integer */
typedef unsigned short int UINT16;	/* 16 bit integer (unsigned) */

/****************************************************************************/
/************************* Message Information ******************************/
/****************************************************************************/

/*
 * Error codes 
 */

/* General errors */
#define EMUSAGE     1		/* Bad args / flags               */
#define EMFAIL      2		/* Unrecoverable error            */
#define EMBADADDR   3		/* Illegal address                */
#define EMBADREG    4		/* Illegal register               */
#define EMSYNTAX    5		/* Illegal command syntax         */
#define EMACCESS    6		/* Could not access memory        */
#define EMALLOC     7		/* Could not allocate memory      */
#define EMTARGET    8		/* Unknown target type            */
#define EMHINIT     9		/* Could not initialize host      */
#define EMCOMM     10		/* Could not open communication channel */

/* Message errors */
#define EMBADMSG   11		/* Unknown message type           */
#define EMMSG2BIG  12		/* Message to large for buffer    */
#define EMNOSEND   13		/* Could not send message         */
#define EMNORECV   14		/* Could not receive message      */

#define EMRESET    15		/* Could not RESET target         */
#define EMCONFIG   16		/* Could not get target CONFIG    */
#define EMSTATUS   17		/* Could not get target STATUS    */
#define EMREAD     18		/* Could not READ target memory   */
#define EMWRITE    19		/* Could not WRITE target memory  */
#define EMBKPTSET  20		/* Could not set breakpoint       */
#define EMBKPTRM   21		/* Could not remove breakpoint    */
#define EMBKPTSTAT 22		/* Could not get breakpoint status */
#define EMBKPTNONE 23		/* All breakpoints in use         */
#define EMBKPTUSED 24		/* Breakpoints already in use     */
#define EMCOPY     25		/* Could not COPY target memory   */
#define EMFILL     26		/* Could not FILL target memory   */
#define EMINIT     27		/* Could not initialize target memory */
#define EMGO       28		/* Could not start execution      */
#define EMSTEP     29		/* Could not single step          */
#define EMBREAK    30		/* Could not BREAK                */
#define EMHIF      31		/* Could not perform HIF service  */
#define EMCHANNEL0 32		/* Could not read CHANNEL0        */
#define EMCHANNEL1 33		/* Could not write CHANNEL1       */

/* COFF file loader errors */
#define EMOPEN     34		/* Could not open COFF file       */
#define EMHDR      35		/* Could not read COFF header     */
#define EMMAGIC    36		/* Bad magic number               */
#define EMAOUT     37		/* Could not read COFF a.out header */
#define EMSCNHDR   38		/* Could not read COFF section header */
#define EMSCN      39		/* Could not read COFF section    */
#define EMCLOSE    40		/* Could not close COFF file      */

/* Log file errors */
#define EMLOGOPEN  41		/* Could not open log file        */
#define EMLOGREAD  42		/* Could not read log file        */
#define EMLOGWRITE 43		/* Could not write to log file    */
#define EMLOGCLOSE 44		/* Could not close log file       */

/* Command file errors */
#define EMCMDOPEN  45		/* Could not open command file    */
#define EMCMDREAD  46		/* Could not read command file    */
#define EMCMDWRITE 47		/* Could not write to command file */
#define EMCMDCLOSE 48		/* Could not close comand file    */

#define EMTIMEOUT  49		/* Host timed out waiting for a message */
#define EMCOMMTYPE 50		/* A '-t' flag must be specified  */
#define EMCOMMERR  51		/* Communication error            */
#define EMBAUD     52		/* Invalid baud rate specified    */
/*
 * Memory Spaces
 */
#define LOCAL_REG    0		/* Local processor register     */
#define GLOBAL_REG   1		/* Global processor register    */
#define SPECIAL_REG  2		/* Special processor register   */
#define TLB_REG      3		/* Translation Lookaside Buffer */
#define COPROC_REG   4		/* Coprocessor register         */
#define I_MEM        5		/* Instruction Memory           */
#define D_MEM        6		/* Data Memory                  */
#define I_ROM        7		/* Instruction ROM              */
#define D_ROM        8		/* Data ROM                     */
#define I_O          9		/* Input/Output                 */
#define I_CACHE     10		/* Instruction Cache            */
#define D_CACHE     11		/* Data Cache                   */

/* To supress warnings for zero length array definitions */
#define DUMMY 1

/*
   ** Host to target definitions
 */

#define RESET          0
#define CONFIG_REQ     1
#define STATUS_REQ     2
#define READ_REQ       3
#define WRITE_REQ      4
#define BKPT_SET       5
#define BKPT_RM        6
#define BKPT_STAT      7
#define COPY           8
#define FILL           9
#define INIT          10
#define GO            11
#define STEP          12
#define BREAK         13

#define HIF_CALL_RTN  64
#define CHANNEL0      65
#define CHANNEL1_ACK  66


/*
   ** Target to host definitions
 */

#define RESET_ACK     32
#define CONFIG        33
#define STATUS        34
#define READ_ACK      35
#define WRITE_ACK     36
#define BKPT_SET_ACK  37
#define BKPT_RM_ACK   38
#define BKPT_STAT_ACK 39
#define COPY_ACK      40
#define FILL_ACK      41
#define INIT_ACK      42
#define HALT          43

#define ERROR         63

#define HIF_CALL      96
#define CHANNEL0_ACK  97
#define CHANNEL1      98


/* A "generic" message */
struct generic_msg_t
  {
    INT32 code;			/* generic */
    INT32 length;
    BYTE byte[DUMMY];
  };


/* A "generic" message (with an INT32 array) */
struct generic_int32_msg_t
  {
    INT32 code;			/* generic */
    INT32 length;
    INT32 int32[DUMMY];
  };


/*
   ** Host to target messages
 */

struct reset_msg_t
  {
    INT32 code;			/* 0 */
    INT32 length;
  };


struct config_req_msg_t
  {
    INT32 code;			/* 1 */
    INT32 length;
  };


struct status_req_msg_t
  {
    INT32 code;			/* 2 */
    INT32 length;
  };


struct read_req_msg_t
  {
    INT32 code;			/* 3 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 byte_count;
  };


struct write_req_msg_t
  {
    INT32 code;			/* 4 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 byte_count;
    BYTE data[DUMMY];
  };


struct write_r_msg_t
  {
    INT32 code;			/* 4 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 byte_count;
    INT32 data[DUMMY];
  };


struct bkpt_set_msg_t
  {
    INT32 code;			/* 5 */
    INT32 length;
    INT32 memory_space;
    ADDR32 bkpt_addr;
    INT32 pass_count;
    INT32 bkpt_type;
  };


struct bkpt_rm_msg_t
  {
    INT32 code;			/* 6 */
    INT32 length;
    INT32 memory_space;
    ADDR32 bkpt_addr;
  };


struct bkpt_stat_msg_t
  {
    INT32 code;			/* 7 */
    INT32 length;
    INT32 memory_space;
    ADDR32 bkpt_addr;
  };


struct copy_msg_t
  {
    INT32 code;			/* 8 */
    INT32 length;
    INT32 source_space;
    ADDR32 source_addr;
    INT32 dest_space;
    ADDR32 dest_addr;
    INT32 byte_count;
  };


struct fill_msg_t
  {
    INT32 code;			/* 9 */
    INT32 length;
    INT32 memory_space;
    ADDR32 start_addr;
    INT32 fill_count;
    INT32 byte_count;
    BYTE fill_data[DUMMY];
  };


struct init_msg_t
  {
    INT32 code;			/* 10 */
    INT32 length;
    ADDR32 text_start;
    ADDR32 text_end;
    ADDR32 data_start;
    ADDR32 data_end;
    ADDR32 entry_point;
    INT32 mem_stack_size;
    INT32 reg_stack_size;
    ADDR32 arg_start;
    INT32 os_control;
  };


struct go_msg_t
  {
    INT32 code;			/* 11 */
    INT32 length;
  };


struct step_msg_t
  {
    INT32 code;			/* 12 */
    INT32 length;
    INT32 count;
  };


struct break_msg_t
  {
    INT32 code;			/* 13 */
    INT32 length;
  };


struct hif_call_rtn_msg_t
  {
    INT32 code;			/* 64 */
    INT32 length;
    INT32 service_number;
    INT32 gr121;
    INT32 gr96;
    INT32 gr97;
  };


struct channel0_msg_t
  {
    INT32 code;			/* 65 */
    INT32 length;
    BYTE data;
  };


struct channel1_ack_msg_t
  {
    INT32 code;			/* 66 */
    INT32 length;
  };


/*
   ** Target to host messages
 */


struct reset_ack_msg_t
  {
    INT32 code;			/* 32 */
    INT32 length;
  };


struct config_msg_t
  {
    INT32 code;			/* 33 */
    INT32 length;
    INT32 processor_id;
    INT32 version;
    ADDR32 I_mem_start;
    INT32 I_mem_size;
    ADDR32 D_mem_start;
    INT32 D_mem_size;
    ADDR32 ROM_start;
    INT32 ROM_size;
    INT32 max_msg_size;
    INT32 max_bkpts;
    INT32 coprocessor;
    INT32 reserved;
  };


struct status_msg_t
  {
    INT32 code;			/* 34 */
    INT32 length;
    INT32 msgs_sent;
    INT32 msgs_received;
    INT32 errors;
    INT32 bkpts_hit;
    INT32 bkpts_free;
    INT32 traps;
    INT32 fills;
    INT32 spills;
    INT32 cycles;
    INT32 reserved;
  };


struct read_ack_msg_t
  {
    INT32 code;			/* 35 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 byte_count;
    BYTE data[DUMMY];
  };

struct read_r_ack_msg_t
  {
    INT32 code;			/* 35 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 byte_count;
    INT32 data[DUMMY];
  };


struct write_ack_msg_t
  {
    INT32 code;			/* 36 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 byte_count;
  };


struct bkpt_set_ack_msg_t
  {
    INT32 code;			/* 37 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 pass_count;
    INT32 bkpt_type;
  };


struct bkpt_rm_ack_msg_t
  {
    INT32 code;			/* 38 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
  };


struct bkpt_stat_ack_msg_t
  {
    INT32 code;			/* 39 */
    INT32 length;
    INT32 memory_space;
    ADDR32 address;
    INT32 pass_count;
    INT32 bkpt_type;
  };


struct copy_ack_msg_t
  {
    INT32 code;			/* 40 */
    INT32 length;
    INT32 source_space;
    ADDR32 source_addr;
    INT32 dest_space;
    ADDR32 dest_addr;
    INT32 byte_count;
  };


struct fill_ack_msg_t
  {
    INT32 code;			/* 41 */
    INT32 length;
    INT32 memory_space;
    ADDR32 start_addr;
    INT32 fill_count;
    INT32 byte_count;
  };


struct init_ack_msg_t
  {
    INT32 code;			/* 42 */
    INT32 length;
  };


struct halt_msg_t
  {
    INT32 code;			/* 43 */
    INT32 length;
    INT32 memory_space;
    ADDR32 pc0;
    ADDR32 pc1;
    INT32 trap_number;
  };


struct error_msg_t
  {
    INT32 code;			/* 63 */
    INT32 length;
    INT32 error_code;
    INT32 memory_space;
    ADDR32 address;
  };


struct hif_call_msg_t
  {
    INT32 code;			/* 96 */
    INT32 length;
    INT32 service_number;
    INT32 lr2;
    INT32 lr3;
    INT32 lr4;
  };


struct channel0_ack_msg_t
  {
    INT32 code;			/* 97 */
    INT32 length;
  };


struct channel1_msg_t
  {
    INT32 code;			/* 98 */
    INT32 length;
    BYTE data[DUMMY];
  };



/*
   ** Union all of the message types together
 */

union msg_t
  {
    struct generic_msg_t generic_msg;
    struct generic_int32_msg_t generic_int32_msg;

    struct reset_msg_t reset_msg;
    struct config_req_msg_t config_req_msg;
    struct status_req_msg_t status_req_msg;
    struct read_req_msg_t read_req_msg;
    struct write_req_msg_t write_req_msg;
    struct write_r_msg_t write_r_msg;
    struct bkpt_set_msg_t bkpt_set_msg;
    struct bkpt_rm_msg_t bkpt_rm_msg;
    struct bkpt_stat_msg_t bkpt_stat_msg;
    struct copy_msg_t copy_msg;
    struct fill_msg_t fill_msg;
    struct init_msg_t init_msg;
    struct go_msg_t go_msg;
    struct step_msg_t step_msg;
    struct break_msg_t break_msg;

    struct hif_call_rtn_msg_t hif_call_rtn_msg;
    struct channel0_msg_t channel0_msg;
    struct channel1_ack_msg_t channel1_ack_msg;

    struct reset_ack_msg_t reset_ack_msg;
    struct config_msg_t config_msg;
    struct status_msg_t status_msg;
    struct read_ack_msg_t read_ack_msg;
    struct read_r_ack_msg_t read_r_ack_msg;
    struct write_ack_msg_t write_ack_msg;
    struct bkpt_set_ack_msg_t bkpt_set_ack_msg;
    struct bkpt_rm_ack_msg_t bkpt_rm_ack_msg;
    struct bkpt_stat_ack_msg_t bkpt_stat_ack_msg;
    struct copy_ack_msg_t copy_ack_msg;
    struct fill_ack_msg_t fill_ack_msg;
    struct init_ack_msg_t init_ack_msg;
    struct halt_msg_t halt_msg;

    struct error_msg_t error_msg;

    struct hif_call_msg_t hif_call_msg;
    struct channel0_ack_msg_t channel0_ack_msg;
    struct channel1_msg_t channel1_msg;
  };
