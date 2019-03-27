/* XMODEM support for GDB, the GNU debugger.
   Copyright 1995, 2000, 2001 Free Software Foundation, Inc.

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
#include "serial.h"
#include "target.h"
#include "xmodem.h"

/* These definitions are for xmodem protocol. */

#define SOH	0x01
#define STX	0x02
#define ACK	0x06
#define NAK	0x15
#define EOT	0x04
#define CANCEL	0x18

static int blknum;		/* XMODEM block number */
static int crcflag;		/* Sez we are using CRC's instead of cksums */

static int
readchar (struct serial *desc, int timeout)
{
  int c;

  c = serial_readchar (desc, timeout);

  if (remote_debug > 0)
    fputc_unfiltered (c, gdb_stdlog);

  if (c >= 0)
    return c;

  if (c == SERIAL_TIMEOUT)
    error ("Timeout reading from remote system.");

  perror_with_name ("xmodem.c:readchar()");
}

#define CRC16 0x1021		/* Generator polynomial (X^16 + X^12 + X^5 + 1) */

static unsigned short *crctab;

/* Call this to init the fast CRC-16 calculation table.  */

static void
crcinit (void)
{
  static int crctab_inited = 0;
  int val;

  if (crctab_inited == 1)
    return;

  crctab = xmalloc (256 * sizeof (short));

  for (val = 0; val <= 255; val++)
    {
      int i;
      unsigned int crc;

      crc = val << 8;

      for (i = 0; i < 8; ++i)
	{
	  crc <<= 1;

	  if (crc & 0x10000)
	    crc ^= CRC16;
	}

      crctab[val] = crc;
    }

  crctab_inited = 1;
}

/* Calculate a CRC-16 for the LEN byte message pointed at by P.  */

static unsigned short
docrc (unsigned char *p, int len)
{
  unsigned short crc = 0;

  while (len-- > 0)
    crc = (crc << 8) ^ crctab[(crc >> 8) ^ *p++];

  return crc;
}

/* Start up the transmit process.  Reset state variables.  Wait for receiver to
   send NAK or CRC request.  */

int
xmodem_init_xfer (struct serial *desc)
{
  int c;
  int i;

  blknum = 1;
  crcflag = 0;
  crcinit ();

  for (i = 1; i <= 10; i++)
    {
      c = readchar (desc, 6);

      switch (c)
	{
	case 'C':
	  crcflag = 1;
	case NAK:
	  return 0;
	default:
	  fprintf_unfiltered (gdb_stderr, "xmodem_init_xfer: Got unexpected character %c (0%o)\n", c, c);
	  continue;
	case CANCEL:		/* target aborted load */
	  fprintf_unfiltered (gdb_stderr, "Got a CANCEL from the target.\n");
	  continue;
	}
    }
  error ("xmodem_init_xfer:  Too many unexpected characters.");
}

/* Take 128 bytes of data and make a packet out of it.

 *      Each packet looks like this:
 *      +-----+-------+-------+------+-----+
 *      | SOH | Seq1. | Seq2. | data | SUM |
 *      +-----+-------+-------+------+-----+
 *      SOH  = 0x01
 *      Seq1 = The sequence number.
 *      Seq2 = The complement of the sequence number.
 *      Data = A 128 bytes of data.
 *      SUM  = Add the contents of the 128 bytes and use the low-order
 *             8 bits of the result.
 *
 * send_xmodem_packet fills in the XMODEM fields of PACKET and sends it to the
 * remote system.  PACKET must be XMODEM_PACKETSIZE bytes long.  The data must
 * start 3 bytes after the beginning of the packet to leave room for the
 * XMODEM header.  LEN is the length of the data portion of the packet (and
 * must be <= 128 bytes).  If it is < 128 bytes, ^Z padding will be added.
 */

void
xmodem_send_packet (struct serial *desc, unsigned char *packet, int len, int hashmark)
{
  int i;
  int retries;
  int pktlen;
  int datasize;

  /* build the packet header */

  packet[1] = blknum;
  packet[2] = ~blknum;

  blknum++;

  if (len <= XMODEM_DATASIZE)
    {
      packet[0] = SOH;
      datasize = XMODEM_DATASIZE;
    }
  else if (len <= XMODEM_1KDATASIZE)
    {
      packet[0] = STX;
      datasize = XMODEM_1KDATASIZE;
    }
  else
    internal_error (__FILE__, __LINE__, "failed internal consistency check");			/* Packet way too large */

  /* Add ^Z padding if packet < 128 (or 1024) bytes */

  memset (packet + 3 + len, '\026', datasize - len);

  if (crcflag)
    {
      int crc;

      crc = docrc (packet + 3, datasize);

      packet[3 + datasize] = crc >> 8;
      packet[3 + datasize + 1] = crc;
      pktlen = datasize + 5;
    }
  else
    {
      int sum;

      sum = 0;
      for (i = 3; i < datasize + 3; i++)
	sum += packet[i];

      packet[3 + datasize] = sum;	/* add the checksum */
      pktlen = datasize + 4;
    }

  for (retries = 3; retries >= 0; retries--)
    {
      int c;

      serial_write (desc, packet, pktlen);

      c = readchar (desc, 3);
      switch (c)
	{
	case ACK:
	  return;
	case NAK:
	  if (!hashmark)
	    continue;
	  putchar_unfiltered ('-');
	  gdb_flush (gdb_stdout);
	  continue;
	case CANCEL:
	  error ("xmodem_send_packet: Transfer aborted by receiver.");
	default:
	  fprintf_unfiltered (gdb_stderr, "xmodem_send_packet: Got unexpected character %c (0%o)\n", c, c);
	  continue;
	}
    }

  serial_write (desc, "\004", 1);	/* Send an EOT */

  error ("xmodem_send_packet:  Excessive retries.");
}

/* Finish off the transfer.  Send out the EOT, and wait for an ACK.  */

void
xmodem_finish_xfer (struct serial *desc)
{
  int retries;

  for (retries = 10; retries >= 0; retries--)
    {
      int c;

      serial_write (desc, "\004", 1);	/* Send an EOT */

      c = readchar (desc, 3);
      switch (c)
	{
	case ACK:
	  return;
	case NAK:
	  continue;
	case CANCEL:
	  error ("xmodem_finish_xfer: Transfer aborted by receiver.");
	default:
	  fprintf_unfiltered (gdb_stderr, "xmodem_send_packet: Got unexpected character %c (0%o)\n", c, c);
	  continue;
	}
    }

  error ("xmodem_finish_xfer:  Excessive retries.");
}
