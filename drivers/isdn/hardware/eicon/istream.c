
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "platform.h"
#if defined(DIVA_ISTREAM) /* { */
#include "pc.h"
#include "pr_pc.h"
#include "di_defs.h"
#include "divasync.h"
#include "di.h"
#if !defined USE_EXTENDED_DEBUGS
  #include "dimaint.h"
#else
  #define dprintf
#endif
#include "dfifo.h"
int diva_istream_write (void* context,
             int   Id,
              void* data,
             int length,
             int final,
            byte usr1,
            byte usr2);
int diva_istream_read (void* context,
            int Id,
            void* data,
            int max_length,
            int* final,
            byte* usr1,
            byte* usr2);
/* -------------------------------------------------------------------
  Does provide iStream interface to the client
   ------------------------------------------------------------------- */
void diva_xdi_provide_istream_info (ADAPTER* a,
                  diva_xdi_stream_interface_t* pi) {
  pi->provided_service = 0;
}
/* ------------------------------------------------------------------
  Does write the data from caller's buffer to the card's
  stream interface.
  If synchronous service was requested, then function
  does return amount of data written to stream.
  'final' does indicate that pice of data to be written is
  final part of frame (necessary only by structured datatransfer)
  return  0 if zero lengh packet was written
  return -1 if stream is full
  ------------------------------------------------------------------ */
int diva_istream_write (void* context,
                int   Id,
                  void* data,
                 int length,
                 int final,
                byte usr1,
                byte usr2) {
 ADAPTER* a = (ADAPTER*)context;
 int written = 0, to_write = -1;
 char tmp[4];
 byte* data_ptr = (byte*)data;
 for (;;) {
  a->ram_in_dw (a,
#ifdef PLATFORM_GT_32BIT
         ULongToPtr(a->tx_stream[Id] + a->tx_pos[Id]),
#else
         (void*)(a->tx_stream[Id] + a->tx_pos[Id]),
#endif
                  (dword*)&tmp[0],
         1);
  if (tmp[0] & DIVA_DFIFO_READY) { /* No free blocks more */
   if (to_write < 0)
    return (-1); /* was not able to write       */
   break;     /* only part of message was written */
  }
  to_write = MIN(length, DIVA_DFIFO_DATA_SZ);
  if (to_write) {
   a->ram_out_buffer (a,
#ifdef PLATFORM_GT_32BIT
            ULongToPtr(a->tx_stream[Id] + a->tx_pos[Id]+4),
#else
              (void*)(a->tx_stream[Id] + a->tx_pos[Id] + 4),
#endif
                         data_ptr,
             (word)to_write);
   length  -= to_write;
   written  += to_write;
   data_ptr += to_write;
  }
  tmp[1] = (char)to_write;
  tmp[0] = (tmp[0] & DIVA_DFIFO_WRAP) |
       DIVA_DFIFO_READY |
       ((!length && final) ? DIVA_DFIFO_LAST : 0);
  if (tmp[0] & DIVA_DFIFO_LAST) {
   tmp[2] = usr1;
   tmp[3] = usr2;
  }
    a->ram_out_dw (a,
#ifdef PLATFORM_GT_32BIT
         ULongToPtr(a->tx_stream[Id] + a->tx_pos[Id]),
#else
           (void*)(a->tx_stream[Id] + a->tx_pos[Id]),
#endif
                   (dword*)&tmp[0],
          1);
  if (tmp[0] & DIVA_DFIFO_WRAP) {
   a->tx_pos[Id]  = 0;
  } else {
   a->tx_pos[Id] += DIVA_DFIFO_STEP;
  }
  if (!length) {
   break;
  }
 }
 return (written);
}
/* -------------------------------------------------------------------
  In case of SYNCRONOUS service:
  Does write data from stream in caller's buffer.
  Does return amount of data written to buffer
  Final flag is set on return if last part of structured frame
  was received
  return 0  if zero packet was received
  return -1 if stream is empty
    return -2 if read buffer does not profide sufficient space
              to accommodate entire segment
  max_length should be at least 68 bytes
  ------------------------------------------------------------------- */
int diva_istream_read (void* context,
                int Id,
                void* data,
                int max_length,
                int* final,
               byte* usr1,
               byte* usr2) {
 ADAPTER* a = (ADAPTER*)context;
 int read = 0, to_read = -1;
 char tmp[4];
 byte* data_ptr = (byte*)data;
 *final = 0;
 for (;;) {
  a->ram_in_dw (a,
#ifdef PLATFORM_GT_32BIT
         ULongToPtr(a->rx_stream[Id] + a->rx_pos[Id]),
#else
         (void*)(a->rx_stream[Id] + a->rx_pos[Id]),
#endif
                  (dword*)&tmp[0],
         1);
  if (tmp[1] > max_length) {
   if (to_read < 0)
    return (-2); /* was not able to read */
   break;
    }
  if (!(tmp[0] & DIVA_DFIFO_READY)) {
   if (to_read < 0)
    return (-1); /* was not able to read */
   break;
  }
  to_read = MIN(max_length, tmp[1]);
  if (to_read) {
   a->ram_in_buffer(a,
#ifdef PLATFORM_GT_32BIT
           ULongToPtr(a->rx_stream[Id] + a->rx_pos[Id] + 4),
#else
            (void*)(a->rx_stream[Id] + a->rx_pos[Id] + 4),
#endif
                       data_ptr,
            (word)to_read);
   max_length -= to_read;
   read     += to_read;
   data_ptr  += to_read;
  }
  if (tmp[0] & DIVA_DFIFO_LAST) {
   *final = 1;
  }
  tmp[0] &= DIVA_DFIFO_WRAP;
    a->ram_out_dw(a,
#ifdef PLATFORM_GT_32BIT
         ULongToPtr(a->rx_stream[Id] + a->rx_pos[Id]),
#else
         (void*)(a->rx_stream[Id] + a->rx_pos[Id]),
#endif
         (dword*)&tmp[0],
         1);
  if (tmp[0] & DIVA_DFIFO_WRAP) {
   a->rx_pos[Id]  = 0;
  } else {
   a->rx_pos[Id] += DIVA_DFIFO_STEP;
  }
  if (*final) {
   if (usr1)
    *usr1 = tmp[2];
   if (usr2)
    *usr2 = tmp[3];
   break;
  }
 }
 return (read);
}
/* ---------------------------------------------------------------------
  Does check if one of streams had caused interrupt and does
  wake up corresponding application
   --------------------------------------------------------------------- */
void pr_stream (ADAPTER * a) {
}
#endif /* } */
