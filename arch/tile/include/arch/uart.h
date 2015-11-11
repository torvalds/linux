/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/* Machine-generated file; do not edit. */

#ifndef __ARCH_UART_H__
#define __ARCH_UART_H__

#include <arch/abi.h>
#include <arch/uart_def.h>

#ifndef __ASSEMBLER__

/* Divisor. */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * Baud Rate Divisor.  Desired_baud_rate = REF_CLK frequency / (baud *
     * 16).
     *                       Note: REF_CLK is always 125 MHz, the default
     * divisor = 68, baud rate = 125M/(68*16) = 115200 baud.
     */
    uint_reg_t divisor    : 12;
    /* Reserved. */
    uint_reg_t __reserved : 52;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved : 52;
    uint_reg_t divisor    : 12;
#endif
  };

  uint_reg_t word;
} UART_DIVISOR_t;

/* FIFO Count. */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * n: n active entries in the receive FIFO (max is 2**8). Each entry has
     * 8 bits.
     * 0: no active entry in the receive FIFO (that is empty).
     */
    uint_reg_t rfifo_count  : 9;
    /* Reserved. */
    uint_reg_t __reserved_0 : 7;
    /*
     * n: n active entries in the transmit FIFO (max is 2**8). Each entry has
     * 8 bits.
     * 0: no active entry in the transmit FIFO (that is empty).
     */
    uint_reg_t tfifo_count  : 9;
    /* Reserved. */
    uint_reg_t __reserved_1 : 7;
    /*
     * n: n active entries in the write FIFO (max is 2**2). Each entry has 8
     * bits.
     * 0: no active entry in the write FIFO (that is empty).
     */
    uint_reg_t wfifo_count  : 3;
    /* Reserved. */
    uint_reg_t __reserved_2 : 29;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_2 : 29;
    uint_reg_t wfifo_count  : 3;
    uint_reg_t __reserved_1 : 7;
    uint_reg_t tfifo_count  : 9;
    uint_reg_t __reserved_0 : 7;
    uint_reg_t rfifo_count  : 9;
#endif
  };

  uint_reg_t word;
} UART_FIFO_COUNT_t;

/* FLAG. */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Reserved. */
    uint_reg_t __reserved_0 : 1;
    /* 1: receive FIFO is empty */
    uint_reg_t rfifo_empty  : 1;
    /* 1: write FIFO is empty. */
    uint_reg_t wfifo_empty  : 1;
    /* 1: transmit FIFO is empty. */
    uint_reg_t tfifo_empty  : 1;
    /* 1: receive FIFO is full. */
    uint_reg_t rfifo_full   : 1;
    /* 1: write FIFO is full. */
    uint_reg_t wfifo_full   : 1;
    /* 1: transmit FIFO is full. */
    uint_reg_t tfifo_full   : 1;
    /* Reserved. */
    uint_reg_t __reserved_1 : 57;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_1 : 57;
    uint_reg_t tfifo_full   : 1;
    uint_reg_t wfifo_full   : 1;
    uint_reg_t rfifo_full   : 1;
    uint_reg_t tfifo_empty  : 1;
    uint_reg_t wfifo_empty  : 1;
    uint_reg_t rfifo_empty  : 1;
    uint_reg_t __reserved_0 : 1;
#endif
  };

  uint_reg_t word;
} UART_FLAG_t;

/*
 * Interrupt Vector Mask.
 * Each bit in this register corresponds to a specific interrupt. When set,
 * the associated interrupt will not be dispatched.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Read data FIFO read and no data available */
    uint_reg_t rdat_err       : 1;
    /* Write FIFO was written but it was full */
    uint_reg_t wdat_err       : 1;
    /* Stop bit not found when current data was received */
    uint_reg_t frame_err      : 1;
    /* Parity error was detected when current data was received */
    uint_reg_t parity_err     : 1;
    /* Data was received but the receive FIFO was full */
    uint_reg_t rfifo_overflow : 1;
    /*
     * An almost full event is reached when data is to be written to the
     * receive FIFO, and the receive FIFO has more than or equal to
     * BUFFER_THRESHOLD.RFIFO_AFULL bytes.
     */
    uint_reg_t rfifo_afull    : 1;
    /* Reserved. */
    uint_reg_t __reserved_0   : 1;
    /* An entry in the transmit FIFO was popped */
    uint_reg_t tfifo_re       : 1;
    /* An entry has been pushed into the receive FIFO */
    uint_reg_t rfifo_we       : 1;
    /* An entry of the write FIFO has been popped */
    uint_reg_t wfifo_re       : 1;
    /* Rshim read receive FIFO in protocol mode */
    uint_reg_t rfifo_err      : 1;
    /*
     * An almost empty event is reached when data is to be read from the
     * transmit FIFO, and the transmit FIFO has less than or equal to
     * BUFFER_THRESHOLD.TFIFO_AEMPTY bytes.
     */
    uint_reg_t tfifo_aempty   : 1;
    /* Reserved. */
    uint_reg_t __reserved_1   : 52;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_1   : 52;
    uint_reg_t tfifo_aempty   : 1;
    uint_reg_t rfifo_err      : 1;
    uint_reg_t wfifo_re       : 1;
    uint_reg_t rfifo_we       : 1;
    uint_reg_t tfifo_re       : 1;
    uint_reg_t __reserved_0   : 1;
    uint_reg_t rfifo_afull    : 1;
    uint_reg_t rfifo_overflow : 1;
    uint_reg_t parity_err     : 1;
    uint_reg_t frame_err      : 1;
    uint_reg_t wdat_err       : 1;
    uint_reg_t rdat_err       : 1;
#endif
  };

  uint_reg_t word;
} UART_INTERRUPT_MASK_t;

/*
 * Interrupt vector, write-one-to-clear.
 * Each bit in this register corresponds to a specific interrupt. Hardware
 * sets the bit when the associated condition has occurred. Writing a 1
 * clears the status bit.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Read data FIFO read and no data available */
    uint_reg_t rdat_err       : 1;
    /* Write FIFO was written but it was full */
    uint_reg_t wdat_err       : 1;
    /* Stop bit not found when current data was received */
    uint_reg_t frame_err      : 1;
    /* Parity error was detected when current data was received */
    uint_reg_t parity_err     : 1;
    /* Data was received but the receive FIFO was full */
    uint_reg_t rfifo_overflow : 1;
    /*
     * Data was received and the receive FIFO is now almost full (more than
     * BUFFER_THRESHOLD.RFIFO_AFULL bytes in it)
     */
    uint_reg_t rfifo_afull    : 1;
    /* Reserved. */
    uint_reg_t __reserved_0   : 1;
    /* An entry in the transmit FIFO was popped */
    uint_reg_t tfifo_re       : 1;
    /* An entry has been pushed into the receive FIFO */
    uint_reg_t rfifo_we       : 1;
    /* An entry of the write FIFO has been popped */
    uint_reg_t wfifo_re       : 1;
    /* Rshim read receive FIFO in protocol mode */
    uint_reg_t rfifo_err      : 1;
    /*
     * Data was read from the transmit FIFO and now it is almost empty (less
     * than or equal to BUFFER_THRESHOLD.TFIFO_AEMPTY bytes in it).
     */
    uint_reg_t tfifo_aempty   : 1;
    /* Reserved. */
    uint_reg_t __reserved_1   : 52;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_1   : 52;
    uint_reg_t tfifo_aempty   : 1;
    uint_reg_t rfifo_err      : 1;
    uint_reg_t wfifo_re       : 1;
    uint_reg_t rfifo_we       : 1;
    uint_reg_t tfifo_re       : 1;
    uint_reg_t __reserved_0   : 1;
    uint_reg_t rfifo_afull    : 1;
    uint_reg_t rfifo_overflow : 1;
    uint_reg_t parity_err     : 1;
    uint_reg_t frame_err      : 1;
    uint_reg_t wdat_err       : 1;
    uint_reg_t rdat_err       : 1;
#endif
  };

  uint_reg_t word;
} UART_INTERRUPT_STATUS_t;

/* Type. */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Number of stop bits, rx and tx */
    uint_reg_t sbits        : 1;
    /* Reserved. */
    uint_reg_t __reserved_0 : 1;
    /* Data word size, rx and tx */
    uint_reg_t dbits        : 1;
    /* Reserved. */
    uint_reg_t __reserved_1 : 1;
    /* Parity selection, rx and tx */
    uint_reg_t ptype        : 3;
    /* Reserved. */
    uint_reg_t __reserved_2 : 57;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_2 : 57;
    uint_reg_t ptype        : 3;
    uint_reg_t __reserved_1 : 1;
    uint_reg_t dbits        : 1;
    uint_reg_t __reserved_0 : 1;
    uint_reg_t sbits        : 1;
#endif
  };

  uint_reg_t word;
} UART_TYPE_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_UART_H__) */
