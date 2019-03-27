/* Remote debugging interface for Densan DVE-R3900 ROM monitor for
   GDB, the GNU debugger.
   Copyright 1997, 1998, 2000, 2001 Free Software Foundation, Inc.

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

#include "defs.h"
#include "gdbcore.h"
#include "target.h"
#include "monitor.h"
#include "serial.h"
#include "inferior.h"
#include "command.h"
#include "gdb_string.h"
#include <time.h>
#include "regcache.h"
#include "mips-tdep.h"

/* Type of function passed to bfd_map_over_sections.  */

typedef void (*section_map_func) (bfd * abfd, asection * sect, void *obj);

/* Packet escape character used by Densan monitor.  */

#define PESC 0xdc

/* Maximum packet size.  This is actually smaller than necessary
   just to be safe.  */

#define MAXPSIZE 1024

/* External functions.  */

extern void report_transfer_performance (unsigned long, time_t, time_t);

/* Certain registers are "bitmapped", in that the monitor can only display
   them or let the user modify them as a series of named bitfields.
   This structure describes a field in a bitmapped register.  */

struct bit_field
  {
    char *prefix;		/* string appearing before the value */
    char *suffix;		/* string appearing after the value */
    char *user_name;		/* name used by human when entering field value */
    int length;			/* number of bits in the field */
    int start;			/* starting (least significant) bit number of field */
  };

/* Local functions for register manipulation.  */

static void r3900_supply_register (char *regname, int regnamelen,
				   char *val, int vallen);
static void fetch_bad_vaddr (void);
static unsigned long fetch_fields (struct bit_field *bf);
static void fetch_bitmapped_register (int regno, struct bit_field *bf);
static void r3900_fetch_registers (int regno);
static void store_bitmapped_register (int regno, struct bit_field *bf);
static void r3900_store_registers (int regno);

/* Local functions for fast binary loading.  */

static void write_long (char *buf, long n);
static void write_long_le (char *buf, long n);
static int debug_readchar (int hex);
static void debug_write (unsigned char *buf, int buflen);
static void ignore_packet (void);
static void send_packet (char type, unsigned char *buf, int buflen, int seq);
static void process_read_request (unsigned char *buf, int buflen);
static void count_section (bfd * abfd, asection * s,
			   unsigned int *section_count);
static void load_section (bfd * abfd, asection * s, unsigned int *data_count);
static void r3900_load (char *filename, int from_tty);

/* Miscellaneous local functions.  */

static void r3900_open (char *args, int from_tty);


/* Pointers to static functions in monitor.c for fetching and storing
   registers.  We can't use these function in certain cases where the Densan
   monitor acts perversely: for registers that it displays in bit-map
   format, and those that can't be modified at all.  In those cases
   we have to use our own functions to fetch and store their values.  */

static void (*orig_monitor_fetch_registers) (int regno);
static void (*orig_monitor_store_registers) (int regno);

/* Pointer to static function in monitor. for loading programs.
   We use this function for loading S-records via the serial link.  */

static void (*orig_monitor_load) (char *file, int from_tty);

/* This flag is set if a fast ethernet download should be used.  */

static int ethernet = 0;

/* This array of registers needs to match the indexes used by GDB. The
   whole reason this exists is because the various ROM monitors use
   different names than GDB does, and don't support all the registers
   either.  */

static char *r3900_regnames[] =
{
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
  "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",

  "S",				/* PS_REGNUM */
  "l",				/* MIPS_EMBED_LO_REGNUM */
  "h",				/* MIPS_EMBED_HI_REGNUM */
  "B",				/* MIPS_EMBED_BADVADDR_REGNUM */
  "Pcause",			/* MIPS_EMBED_CAUSE_REGNUM */
  "p"				/* MIPS_EMBED_PC_REGNUM */
};


/* Table of register names produced by monitor's register dump command.  */

static struct reg_entry
  {
    char *name;
    int regno;
  }
reg_table[] =
{
  {
    "r0_zero", 0
  }
  ,
  {
    "r1_at", 1
  }
  ,
  {
    "r2_v0", 2
  }
  ,
  {
    "r3_v1", 3
  }
  ,
  {
    "r4_a0", 4
  }
  ,
  {
    "r5_a1", 5
  }
  ,
  {
    "r6_a2", 6
  }
  ,
  {
    "r7_a3", 7
  }
  ,
  {
    "r8_t0", 8
  }
  ,
  {
    "r9_t1", 9
  }
  ,
  {
    "r10_t2", 10
  }
  ,
  {
    "r11_t3", 11
  }
  ,
  {
    "r12_t4", 12
  }
  ,
  {
    "r13_t5", 13
  }
  ,
  {
    "r14_t6", 14
  }
  ,
  {
    "r15_t7", 15
  }
  ,
  {
    "r16_s0", 16
  }
  ,
  {
    "r17_s1", 17
  }
  ,
  {
    "r18_s2", 18
  }
  ,
  {
    "r19_s3", 19
  }
  ,
  {
    "r20_s4", 20
  }
  ,
  {
    "r21_s5", 21
  }
  ,
  {
    "r22_s6", 22
  }
  ,
  {
    "r23_s7", 23
  }
  ,
  {
    "r24_t8", 24
  }
  ,
  {
    "r25_t9", 25
  }
  ,
  {
    "r26_k0", 26
  }
  ,
  {
    "r27_k1", 27
  }
  ,
  {
    "r28_gp", 28
  }
  ,
  {
    "r29_sp", 29
  }
  ,
  {
    "r30_fp", 30
  }
  ,
  {
    "r31_ra", 31
  }
  ,
  {
    "HI", MIPS_EMBED_HI_REGNUM
  }
  ,
  {
    "LO", MIPS_EMBED_LO_REGNUM
  }
  ,
  {
    "PC", MIPS_EMBED_PC_REGNUM
  }
  ,
  {
    "BadV", MIPS_EMBED_BADVADDR_REGNUM
  }
  ,
  {
    NULL, 0
  }
};


/* The monitor displays the cache register along with the status register,
   as if they were a single register.  So when we want to fetch the
   status register, parse but otherwise ignore the fields of the
   cache register that the monitor displays.  Register fields that should
   be ignored have a length of zero in the tables below.  */

static struct bit_field status_fields[] =
{
  /* Status register portion */
  {"SR[<CU=", " ", "cu", 4, 28},
  {"RE=", " ", "re", 1, 25},
  {"BEV=", " ", "bev", 1, 22},
  {"TS=", " ", "ts", 1, 21},
  {"Nmi=", " ", "nmi", 1, 20},
  {"INT=", " ", "int", 6, 10},
  {"SW=", ">]", "sw", 2, 8},
  {"[<KUO=", " ", "kuo", 1, 5},
  {"IEO=", " ", "ieo", 1, 4},
  {"KUP=", " ", "kup", 1, 3},
  {"IEP=", " ", "iep", 1, 2},
  {"KUC=", " ", "kuc", 1, 1},
  {"IEC=", ">]", "iec", 1, 0},

  /* Cache register portion (dummy for parsing only) */
  {"CR[<IalO=", " ", "ialo", 0, 13},
  {"DalO=", " ", "dalo", 0, 12},
  {"IalP=", " ", "ialp", 0, 11},
  {"DalP=", " ", "dalp", 0, 10},
  {"IalC=", " ", "ialc", 0, 9},
  {"DalC=", ">] ", "dalc", 0, 8},

  {NULL, NULL, 0, 0}		/* end of table marker */
};


#if 0				/* FIXME: Enable when we add support for modifying cache register.  */
static struct bit_field cache_fields[] =
{
  /* Status register portion (dummy for parsing only) */
  {"SR[<CU=", " ", "cu", 0, 28},
  {"RE=", " ", "re", 0, 25},
  {"BEV=", " ", "bev", 0, 22},
  {"TS=", " ", "ts", 0, 21},
  {"Nmi=", " ", "nmi", 0, 20},
  {"INT=", " ", "int", 0, 10},
  {"SW=", ">]", "sw", 0, 8},
  {"[<KUO=", " ", "kuo", 0, 5},
  {"IEO=", " ", "ieo", 0, 4},
  {"KUP=", " ", "kup", 0, 3},
  {"IEP=", " ", "iep", 0, 2},
  {"KUC=", " ", "kuc", 0, 1},
  {"IEC=", ">]", "iec", 0, 0},

  /* Cache register portion  */
  {"CR[<IalO=", " ", "ialo", 1, 13},
  {"DalO=", " ", "dalo", 1, 12},
  {"IalP=", " ", "ialp", 1, 11},
  {"DalP=", " ", "dalp", 1, 10},
  {"IalC=", " ", "ialc", 1, 9},
  {"DalC=", ">] ", "dalc", 1, 8},

  {NULL, NULL, NULL, 0, 0}	/* end of table marker */
};
#endif


static struct bit_field cause_fields[] =
{
  {"<BD=", " ", "bd", 1, 31},
  {"CE=", " ", "ce", 2, 28},
  {"IP=", " ", "ip", 6, 10},
  {"SW=", " ", "sw", 2, 8},
  {"EC=", ">]", "ec", 5, 2},

  {NULL, NULL, NULL, 0, 0}	/* end of table marker */
};


/* The monitor prints register values in the form

   regname = xxxx xxxx

   We look up the register name in a table, and remove the embedded space in
   the hex value before passing it to monitor_supply_register.  */

static void
r3900_supply_register (char *regname, int regnamelen, char *val, int vallen)
{
  int regno = -1;
  int i;
  char valbuf[10];
  char *p;

  /* Perform some sanity checks on the register name and value.  */
  if (regnamelen < 2 || regnamelen > 7 || vallen != 9)
    return;

  /* Look up the register name.  */
  for (i = 0; reg_table[i].name != NULL; i++)
    {
      int rlen = strlen (reg_table[i].name);
      if (rlen == regnamelen && strncmp (regname, reg_table[i].name, rlen) == 0)
	{
	  regno = reg_table[i].regno;
	  break;
	}
    }
  if (regno == -1)
    return;

  /* Copy the hex value to a buffer and eliminate the embedded space. */
  for (i = 0, p = valbuf; i < vallen; i++)
    if (val[i] != ' ')
      *p++ = val[i];
  *p = '\0';

  monitor_supply_register (regno, valbuf);
}


/* Fetch the BadVaddr register.  Unlike the other registers, this
   one can't be modified, and the monitor won't even prompt to let
   you modify it.  */

static void
fetch_bad_vaddr (void)
{
  char buf[20];

  monitor_printf ("xB\r");
  monitor_expect ("BadV=", NULL, 0);
  monitor_expect_prompt (buf, sizeof (buf));
  monitor_supply_register (mips_regnum (current_gdbarch)->badvaddr, buf);
}


/* Read a series of bit fields from the monitor, and return their
   combined binary value.  */

static unsigned long
fetch_fields (struct bit_field *bf)
{
  char buf[20];
  unsigned long val = 0;
  unsigned long bits;

  for (; bf->prefix != NULL; bf++)
    {
      monitor_expect (bf->prefix, NULL, 0);	/* get prefix */
      monitor_expect (bf->suffix, buf, sizeof (buf));	/* hex value, suffix */
      if (bf->length != 0)
	{
	  bits = strtoul (buf, NULL, 16);	/* get field value */
	  bits &= ((1 << bf->length) - 1);	/* mask out useless bits */
	  val |= bits << bf->start;	/* insert into register */
	}

    }

  return val;
}


static void
fetch_bitmapped_register (int regno, struct bit_field *bf)
{
  unsigned long val;
  unsigned char regbuf[MAX_REGISTER_SIZE];
  char *regname = NULL;

  if (regno >= sizeof (r3900_regnames) / sizeof (r3900_regnames[0]))
    internal_error (__FILE__, __LINE__,
                    "fetch_bitmapped_register: regno out of bounds");
  else
    regname = r3900_regnames[regno];

  monitor_printf ("x%s\r", regname);
  val = fetch_fields (bf);
  monitor_printf (".\r");
  monitor_expect_prompt (NULL, 0);

  /* supply register stores in target byte order, so swap here */

  store_unsigned_integer (regbuf, DEPRECATED_REGISTER_RAW_SIZE (regno), val);
  supply_register (regno, regbuf);

}


/* Fetch all registers (if regno is -1), or one register from the
   monitor.  For most registers, we can use the generic monitor_
   monitor_fetch_registers function.  But others are displayed in
   a very unusual fashion by the monitor, and must be handled specially.  */

static void
r3900_fetch_registers (int regno)
{
  if (regno == mips_regnum (current_gdbarch)->badvaddr)
    fetch_bad_vaddr ();
  else if (regno == PS_REGNUM)
    fetch_bitmapped_register (PS_REGNUM, status_fields);
  else if (regno == mips_regnum (current_gdbarch)->cause)
    fetch_bitmapped_register (mips_regnum (current_gdbarch)->cause,
			      cause_fields);
  else
    orig_monitor_fetch_registers (regno);
}


/* Write the new value of the bitmapped register to the monitor.  */

static void
store_bitmapped_register (int regno, struct bit_field *bf)
{
  unsigned long oldval, newval;
  char *regname = NULL;

  if (regno >= sizeof (r3900_regnames) / sizeof (r3900_regnames[0]))
    internal_error (__FILE__, __LINE__,
                    "fetch_bitmapped_register: regno out of bounds");
  else
    regname = r3900_regnames[regno];

  /* Fetch the current value of the register.  */
  monitor_printf ("x%s\r", regname);
  oldval = fetch_fields (bf);
  newval = read_register (regno);

  /* To save time, write just the fields that have changed.  */
  for (; bf->prefix != NULL; bf++)
    {
      if (bf->length != 0)
	{
	  unsigned long oldbits, newbits, mask;

	  mask = (1 << bf->length) - 1;
	  oldbits = (oldval >> bf->start) & mask;
	  newbits = (newval >> bf->start) & mask;
	  if (oldbits != newbits)
	    monitor_printf ("%s %lx ", bf->user_name, newbits);
	}
    }

  monitor_printf (".\r");
  monitor_expect_prompt (NULL, 0);
}


static void
r3900_store_registers (int regno)
{
  if (regno == PS_REGNUM)
    store_bitmapped_register (PS_REGNUM, status_fields);
  else if (regno == mips_regnum (current_gdbarch)->cause)
    store_bitmapped_register (mips_regnum (current_gdbarch)->cause,
			      cause_fields);
  else
    orig_monitor_store_registers (regno);
}


/* Write a 4-byte integer to the buffer in big-endian order.  */

static void
write_long (char *buf, long n)
{
  buf[0] = (n >> 24) & 0xff;
  buf[1] = (n >> 16) & 0xff;
  buf[2] = (n >> 8) & 0xff;
  buf[3] = n & 0xff;
}


/* Write a 4-byte integer to the buffer in little-endian order.  */

static void
write_long_le (char *buf, long n)
{
  buf[0] = n & 0xff;
  buf[1] = (n >> 8) & 0xff;
  buf[2] = (n >> 16) & 0xff;
  buf[3] = (n >> 24) & 0xff;
}


/* Read a character from the monitor.  If remote debugging is on,
   print the received character.  If HEX is non-zero, print the
   character in hexadecimal; otherwise, print it in ASCII.  */

static int
debug_readchar (int hex)
{
  char buf[10];
  int c = monitor_readchar ();

  if (remote_debug > 0)
    {
      if (hex)
	sprintf (buf, "[%02x]", c & 0xff);
      else if (c == '\0')
	strcpy (buf, "\\0");
      else
	{
	  buf[0] = c;
	  buf[1] = '\0';
	}
      puts_debug ("Read -->", buf, "<--");
    }
  return c;
}


/* Send a buffer of characters to the monitor.  If remote debugging is on,
   print the sent buffer in hex.  */

static void
debug_write (unsigned char *buf, int buflen)
{
  char s[10];

  monitor_write (buf, buflen);

  if (remote_debug > 0)
    {
      while (buflen-- > 0)
	{
	  sprintf (s, "[%02x]", *buf & 0xff);
	  puts_debug ("Sent -->", s, "<--");
	  buf++;
	}
    }
}


/* Ignore a packet sent to us by the monitor.  It send packets
   when its console is in "communications interface" mode.   A packet
   is of this form:

   start of packet flag (one byte: 0xdc)
   packet type (one byte)
   length (low byte)
   length (high byte)
   data (length bytes)

   The last two bytes of the data field are a checksum, but we don't
   bother to verify it.
 */

static void
ignore_packet (void)
{
  int c = -1;
  int len;

  /* Ignore lots of trash (messages about section addresses, for example)
     until we see the start of a packet.  */
  for (len = 0; len < 256; len++)
    {
      c = debug_readchar (0);
      if (c == PESC)
	break;
    }
  if (len == 8)
    error ("Packet header byte not found; %02x seen instead.", c);

  /* Read the packet type and length.  */
  c = debug_readchar (1);	/* type */

  c = debug_readchar (1);	/* low byte of length */
  len = c & 0xff;

  c = debug_readchar (1);	/* high byte of length */
  len += (c & 0xff) << 8;

  /* Ignore the rest of the packet.  */
  while (len-- > 0)
    c = debug_readchar (1);
}


/* Encapsulate some data into a packet and send it to the monitor.

   The 'p' packet is a special case.  This is a packet we send
   in response to a read ('r') packet from the monitor.  This function
   appends a one-byte sequence number to the data field of such a packet.
 */

static void
send_packet (char type, unsigned char *buf, int buflen, int seq)
{
  unsigned char hdr[4];
  int len = buflen;
  int sum, i;

  /* If this is a 'p' packet, add one byte for a sequence number.  */
  if (type == 'p')
    len++;

  /* If the buffer has a non-zero length, add two bytes for a checksum.  */
  if (len > 0)
    len += 2;

  /* Write the packet header.  */
  hdr[0] = PESC;
  hdr[1] = type;
  hdr[2] = len & 0xff;
  hdr[3] = (len >> 8) & 0xff;
  debug_write (hdr, sizeof (hdr));

  if (len)
    {
      /* Write the packet data.  */
      debug_write (buf, buflen);

      /* Write the sequence number if this is a 'p' packet.  */
      if (type == 'p')
	{
	  hdr[0] = seq;
	  debug_write (hdr, 1);
	}

      /* Write the checksum.  */
      sum = 0;
      for (i = 0; i < buflen; i++)
	{
	  int tmp = (buf[i] & 0xff);
	  if (i & 1)
	    sum += tmp;
	  else
	    sum += tmp << 8;
	}
      if (type == 'p')
	{
	  if (buflen & 1)
	    sum += (seq & 0xff);
	  else
	    sum += (seq & 0xff) << 8;
	}
      sum = (sum & 0xffff) + ((sum >> 16) & 0xffff);
      sum += (sum >> 16) & 1;
      sum = ~sum;

      hdr[0] = (sum >> 8) & 0xff;
      hdr[1] = sum & 0xff;
      debug_write (hdr, 2);
    }
}


/* Respond to an expected read request from the monitor by sending
   data in chunks.  Handle all acknowledgements and handshaking packets.

   The monitor expects a response consisting of a one or more 'p' packets,
   each followed by a portion of the data requested.  The 'p' packet
   contains only a four-byte integer, the value of which is the number
   of bytes of data we are about to send.  Following the 'p' packet,
   the monitor expects the data bytes themselves in raw, unpacketized,
   form, without even a checksum.
 */

static void
process_read_request (unsigned char *buf, int buflen)
{
  unsigned char len[4];
  int i, chunk;
  unsigned char seq;

  /* Discard the read request.  FIXME: we have to hope it's for
     the exact number of bytes we want to send; should check for this.  */
  ignore_packet ();

  for (i = chunk = 0, seq = 0; i < buflen; i += chunk, seq++)
    {
      /* Don't send more than MAXPSIZE bytes at a time.  */
      chunk = buflen - i;
      if (chunk > MAXPSIZE)
	chunk = MAXPSIZE;

      /* Write a packet containing the number of bytes we are sending.  */
      write_long_le (len, chunk);
      send_packet ('p', len, sizeof (len), seq);

      /* Write the data in raw form following the packet.  */
      debug_write (&buf[i], chunk);

      /* Discard the ACK packet.  */
      ignore_packet ();
    }

  /* Send an "end of data" packet.  */
  send_packet ('e', "", 0, 0);
}


/* Count loadable sections (helper function for r3900_load).  */

static void
count_section (bfd *abfd, asection *s, unsigned int *section_count)
{
  if (s->flags & SEC_LOAD && bfd_section_size (abfd, s) != 0)
    (*section_count)++;
}


/* Load a single BFD section (helper function for r3900_load).

   WARNING: this code is filled with assumptions about how
   the Densan monitor loads programs.  The monitor issues
   packets containing read requests, but rather than respond
   to them in an general way, we expect them to following
   a certain pattern.

   For example, we know that the monitor will start loading by
   issuing an 8-byte read request for the binary file header.
   We know this is coming and ignore the actual contents
   of the read request packet.
 */

static void
load_section (bfd *abfd, asection *s, unsigned int *data_count)
{
  if (s->flags & SEC_LOAD)
    {
      bfd_size_type section_size = bfd_section_size (abfd, s);
      bfd_vma section_base = bfd_section_lma (abfd, s);
      unsigned char *buffer;
      unsigned char header[8];

      /* Don't output zero-length sections.  */
      if (section_size == 0)
	return;
      if (data_count)
	*data_count += section_size;

      /* Print some fluff about the section being loaded.  */
      printf_filtered ("Loading section %s, size 0x%lx lma ",
		       bfd_section_name (abfd, s), (long) section_size);
      print_address_numeric (section_base, 1, gdb_stdout);
      printf_filtered ("\n");
      gdb_flush (gdb_stdout);

      /* Write the section header (location and size).  */
      write_long (&header[0], (long) section_base);
      write_long (&header[4], (long) section_size);
      process_read_request (header, sizeof (header));

      /* Read the section contents into a buffer, write it out,
         then free the buffer.  */
      buffer = (unsigned char *) xmalloc (section_size);
      bfd_get_section_contents (abfd, s, buffer, 0, section_size);
      process_read_request (buffer, section_size);
      xfree (buffer);
    }
}


/* When the ethernet is used as the console port on the Densan board,
   we can use the "Rm" command to do a fast binary load.  The format
   of the download data is:

   number of sections (4 bytes)
   starting address (4 bytes)
   repeat for each section:
   location address (4 bytes)
   section size (4 bytes)
   binary data

   The 4-byte fields are all in big-endian order.

   Using this command is tricky because we have to put the monitor
   into a special funky "communications interface" mode, in which
   it sends and receives packets of data along with the normal prompt.
 */

static void
r3900_load (char *filename, int from_tty)
{
  bfd *abfd;
  unsigned int data_count = 0;
  time_t start_time, end_time;	/* for timing of download */
  int section_count = 0;
  unsigned char buffer[8];

  /* If we are not using the ethernet, use the normal monitor load,
     which sends S-records over the serial link.  */
  if (!ethernet)
    {
      orig_monitor_load (filename, from_tty);
      return;
    }

  /* Open the file.  */
  if (filename == NULL || filename[0] == 0)
    filename = get_exec_file (1);
  abfd = bfd_openr (filename, 0);
  if (!abfd)
    error ("Unable to open file %s\n", filename);
  if (bfd_check_format (abfd, bfd_object) == 0)
    error ("File is not an object file\n");

  /* Output the "vconsi" command to get the monitor in the communication
     state where it will accept a load command.  This will cause
     the monitor to emit a packet before each prompt, so ignore the packet.  */
  monitor_printf ("vconsi\r");
  ignore_packet ();
  monitor_expect_prompt (NULL, 0);

  /* Output the "Rm" (load) command and respond to the subsequent "open"
     packet by sending an ACK packet.  */
  monitor_printf ("Rm\r");
  ignore_packet ();
  send_packet ('a', "", 0, 0);

  /* Output the fast load header (number of sections and starting address).  */
  bfd_map_over_sections ((bfd *) abfd, (section_map_func) count_section,
			 &section_count);
  write_long (&buffer[0], (long) section_count);
  if (exec_bfd)
    write_long (&buffer[4], (long) bfd_get_start_address (exec_bfd));
  else
    write_long (&buffer[4], 0);
  process_read_request (buffer, sizeof (buffer));

  /* Output the section data.  */
  start_time = time (NULL);
  bfd_map_over_sections (abfd, (section_map_func) load_section, &data_count);
  end_time = time (NULL);

  /* Acknowledge the close packet and put the monitor back into
     "normal" mode so it won't send packets any more.  */
  ignore_packet ();
  send_packet ('a', "", 0, 0);
  monitor_expect_prompt (NULL, 0);
  monitor_printf ("vconsx\r");
  monitor_expect_prompt (NULL, 0);

  /* Print start address and download performance information.  */
  printf_filtered ("Start address 0x%lx\n", (long) bfd_get_start_address (abfd));
  report_transfer_performance (data_count, start_time, end_time);

  /* Finally, make the PC point at the start address */
  if (exec_bfd)
    write_pc (bfd_get_start_address (exec_bfd));

  inferior_ptid = null_ptid;		/* No process now */

  /* This is necessary because many things were based on the PC at the
     time that we attached to the monitor, which is no longer valid
     now that we have loaded new code (and just changed the PC).
     Another way to do this might be to call normal_stop, except that
     the stack may not be valid, and things would get horribly
     confused... */
  clear_symtab_users ();
}


/* Commands to send to the monitor when first connecting:
   * The bare carriage return forces a prompt from the monitor
   (monitor doesn't prompt immediately after a reset).
   * The "vconsx" switches the monitor back to interactive mode
   in case an aborted download had left it in packet mode.
   * The "Xtr" command causes subsequent "t" (trace) commands to display
   the general registers only.
   * The "Xxr" command does the same thing for the "x" (examine
   registers) command.
   * The "bx" command clears all breakpoints.
 */

static char *r3900_inits[] =
{"\r", "vconsx\r", "Xtr\r", "Xxr\r", "bx\r", NULL};
static char *dummy_inits[] =
{NULL};

static struct target_ops r3900_ops;
static struct monitor_ops r3900_cmds;

static void
r3900_open (char *args, int from_tty)
{
  char buf[64];
  int i;

  monitor_open (args, &r3900_cmds, from_tty);

  /* We have to handle sending the init strings ourselves, because
     the first two strings we send (carriage returns) may not be echoed
     by the monitor, but the rest will be.  */
  monitor_printf_noecho ("\r\r");
  for (i = 0; r3900_inits[i] != NULL; i++)
    {
      monitor_printf (r3900_inits[i]);
      monitor_expect_prompt (NULL, 0);
    }

  /* Attempt to determine whether the console device is ethernet or serial.
     This will tell us which kind of load to use (S-records over a serial
     link, or the Densan fast binary multi-section format over the net).  */

  ethernet = 0;
  monitor_printf ("v\r");
  if (monitor_expect ("console device :", NULL, 0) != -1)
    if (monitor_expect ("\n", buf, sizeof (buf)) != -1)
      if (strstr (buf, "ethernet") != NULL)
	ethernet = 1;
  monitor_expect_prompt (NULL, 0);
}

void
_initialize_r3900_rom (void)
{
  r3900_cmds.flags = MO_NO_ECHO_ON_OPEN |
    MO_ADDR_BITS_REMOVE |
    MO_CLR_BREAK_USES_ADDR |
    MO_GETMEM_READ_SINGLE |
    MO_PRINT_PROGRAM_OUTPUT;

  r3900_cmds.init = dummy_inits;
  r3900_cmds.cont = "g\r";
  r3900_cmds.step = "t\r";
  r3900_cmds.set_break = "b %A\r";	/* COREADDR */
  r3900_cmds.clr_break = "b %A,0\r";	/* COREADDR */
  r3900_cmds.fill = "fx %A s %x %x\r";	/* COREADDR, len, val */

  r3900_cmds.setmem.cmdb = "sx %A %x\r";	/* COREADDR, val */
  r3900_cmds.setmem.cmdw = "sh %A %x\r";	/* COREADDR, val */
  r3900_cmds.setmem.cmdl = "sw %A %x\r";	/* COREADDR, val */

  r3900_cmds.getmem.cmdb = "sx %A\r";	/* COREADDR */
  r3900_cmds.getmem.cmdw = "sh %A\r";	/* COREADDR */
  r3900_cmds.getmem.cmdl = "sw %A\r";	/* COREADDR */
  r3900_cmds.getmem.resp_delim = " : ";
  r3900_cmds.getmem.term = " ";
  r3900_cmds.getmem.term_cmd = ".\r";

  r3900_cmds.setreg.cmd = "x%s %x\r";	/* regname, val */

  r3900_cmds.getreg.cmd = "x%s\r";	/* regname */
  r3900_cmds.getreg.resp_delim = "=";
  r3900_cmds.getreg.term = " ";
  r3900_cmds.getreg.term_cmd = ".\r";

  r3900_cmds.dump_registers = "x\r";
  r3900_cmds.register_pattern =
    "\\([a-zA-Z0-9_]+\\) *=\\([0-9a-f]+ [0-9a-f]+\\b\\)";
  r3900_cmds.supply_register = r3900_supply_register;
  /* S-record download, via "keyboard port".  */
  r3900_cmds.load = "r0\r";
  r3900_cmds.prompt = "#";
  r3900_cmds.line_term = "\r";
  r3900_cmds.target = &r3900_ops;
  r3900_cmds.stopbits = SERIAL_1_STOPBITS;
  r3900_cmds.regnames = r3900_regnames;
  r3900_cmds.magic = MONITOR_OPS_MAGIC;

  init_monitor_ops (&r3900_ops);

  r3900_ops.to_shortname = "r3900";
  r3900_ops.to_longname = "R3900 monitor";
  r3900_ops.to_doc = "Debug using the DVE R3900 monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  r3900_ops.to_open = r3900_open;

  /* Override the functions to fetch and store registers.  But save the
     addresses of the default functions, because we will use those functions
     for "normal" registers.  */

  orig_monitor_fetch_registers = r3900_ops.to_fetch_registers;
  orig_monitor_store_registers = r3900_ops.to_store_registers;
  r3900_ops.to_fetch_registers = r3900_fetch_registers;
  r3900_ops.to_store_registers = r3900_store_registers;

  /* Override the load function, but save the address of the default
     function to use when loading S-records over a serial link.  */
  orig_monitor_load = r3900_ops.to_load;
  r3900_ops.to_load = r3900_load;

  add_target (&r3900_ops);
}
