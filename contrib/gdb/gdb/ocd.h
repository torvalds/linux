/* Definitions for the Macraigor Systems BDM Wiggler
   Copyright 1996, 1997, 1998, 2000, 2001 Free Software Foundation, Inc.

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

#ifndef OCD_H
#define OCD_H

struct mem_attrib;
struct target_ops;

/* Wiggler serial protocol definitions */

#define DLE 020			/* Quote char */
#define SYN 026			/* Start of packet */
#define RAW_SYN ((026 << 8) | 026)	/* get_quoted_char found a naked SYN */

/* Status flags */

#define OCD_FLAG_RESET 0x01	/* Target is being reset */
#define OCD_FLAG_STOPPED 0x02	/* Target is halted */
#define OCD_FLAG_BDM 0x04	/* Target is in BDM */
#define OCD_FLAG_PWF 0x08	/* Power failed */
#define OCD_FLAG_CABLE_DISC 0x10	/* BDM cable disconnected */

/* Commands */

#define OCD_AYT 0x0		/* Are you there? */
#define OCD_GET_VERSION 0x1	/* Get Version */
#define OCD_SET_BAUD_RATE 0x2	/* Set Baud Rate */
#define OCD_INIT 0x10		/* Initialize Wiggler */
#define OCD_SET_SPEED 0x11	/* Set Speed */
#define OCD_GET_STATUS_MASK 0x12	/* Get Status Mask */
#define OCD_GET_CTRS 0x13	/* Get Error Counters */
#define OCD_SET_FUNC_CODE 0x14	/* Set Function Code */
#define OCD_SET_CTL_FLAGS 0x15	/* Set Control Flags */
#define OCD_SET_BUF_ADDR 0x16	/* Set Register Buffer Address */
#define OCD_RUN 0x20		/* Run Target from PC */
#define OCD_RUN_ADDR 0x21	/* Run Target from Specified Address */
#define OCD_STOP 0x22		/* Stop Target */
#define OCD_RESET_RUN 0x23	/* Reset Target and Run */
#define OCD_RESET 0x24		/* Reset Target and Halt */
#define OCD_STEP 0x25		/* Single step */
#define OCD_READ_REGS 0x30	/* Read Registers */
#define OCD_WRITE_REGS 0x31	/* Write Registers */
#define OCD_READ_MEM 0x32	/* Read Memory */
#define OCD_WRITE_MEM 0x33	/* Write Memory */
#define OCD_FILL_MEM 0x34	/* Fill Memory */
#define OCD_MOVE_MEM 0x35	/* Move Memory */

#define OCD_READ_INT_MEM 0x80	/* Read Internal Memory */
#define OCD_WRITE_INT_MEM 0x81	/* Write Internal Memory */
#define OCD_JUMP 0x82		/* Jump to Subroutine */

#define OCD_ERASE_FLASH 0x90	/* Erase flash memory */
#define OCD_PROGRAM_FLASH 0x91	/* Write flash memory */
#define OCD_EXIT_MON 0x93	/* Exit the flash programming monitor  */
#define OCD_ENTER_MON 0x94	/* Enter the flash programming monitor  */

#define OCD_SET_STATUS 0x0a	/* Set status */
#define OCD_SET_CONNECTION 0xf0	/* Set connection (init Wigglers.dll) */
#define OCD_LOG_FILE 0xf1	/* Cmd to get Wigglers.dll to log cmds */
#define OCD_FLAG_STOP 0x0	/* Stop the target, enter BDM */
#define OCD_FLAG_START 0x01	/* Start the target at PC */
#define OCD_FLAG_RETURN_STATUS 0x04	/* Return async status */

/* Target type (for OCD_INIT command) */

enum ocd_target_type
  {
    OCD_TARGET_CPU32 = 0x0,	/* Moto cpu32 family */
    OCD_TARGET_CPU16 = 0x1,
    OCD_TARGET_MOTO_PPC = 0x2,	/* Motorola PPC 5xx/8xx */
    OCD_TARGET_IBM_PPC = 0x3
  };				/* IBM PPC 4xx */

void ocd_open (char *name, int from_tty, enum ocd_target_type,
	       struct target_ops *ops);

void ocd_close (int quitting);

void ocd_detach (char *args, int from_tty);

void ocd_resume (ptid_t ptid, int step, enum target_signal siggnal);

void ocd_prepare_to_store (void);

void ocd_stop (void);

void ocd_files_info (struct target_ops *ignore);


int ocd_xfer_memory (CORE_ADDR memaddr, char *myaddr,
		     int len, int should_write,
		     struct mem_attrib *attrib,
		     struct target_ops *target);

void ocd_mourn (void);

void ocd_create_inferior (char *exec_file, char *args, char **env);

int ocd_thread_alive (ptid_t th);

void ocd_error (char *s, int error_code);

void ocd_kill (void);

void ocd_load (char *args, int from_tty);

unsigned char *ocd_read_bdm_registers (int first_bdm_regno,
				       int last_bdm_regno, int *reglen);

CORE_ADDR ocd_read_bdm_register (int bdm_regno);

void ocd_write_bdm_registers (int first_bdm_regno,
			      unsigned char *regptr, int reglen);

void ocd_write_bdm_register (int bdm_regno, CORE_ADDR reg);

int ocd_wait (void);

int ocd_insert_breakpoint (CORE_ADDR addr, char *contents_cache);
int ocd_remove_breakpoint (CORE_ADDR addr, char *contents_cache);

int ocd_write_bytes (CORE_ADDR memaddr, char *myaddr, int len);

#endif /* OCD_H */
