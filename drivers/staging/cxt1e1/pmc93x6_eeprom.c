/* pmc93x6_eeprom.c - PMC's 93LC46 EEPROM Device
 *
 *    The 93LC46 is a low-power, serial Electrically Erasable and
 *    Programmable Read Only Memory organized as 128 8-bit bytes.
 *
 *    Accesses to the 93LC46 are done in a bit serial stream, organized
 *    in a 3 wire format.  Writes are internally timed by the device
 *    (the In data bit is pulled low until the write is complete and
 *    then is pulled high) and take about 6 milliseconds.
 *
 * Copyright (C) 2003-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "pmcc4.h"
#include "sbe_promformat.h"

#ifndef TRUE
#define TRUE   1
#define FALSE  0
#endif

/*------------------------------------------------------------------------
 *      EEPROM address definitions
 *------------------------------------------------------------------------
 *
 *      The offset in the definitions below allows the test to skip over
 *      areas of the EEPROM that other programs (such a VxWorks) are
 *      using.
 */

#define EE_MFG      (long)0     /* Index to manufacturing record */
#define EE_FIRST    0x28        /* Index to start testing at */
#define EE_LIMIT    128         /* Index to end testing at */


/*  Bit Ordering for Instructions
**
**  A0, A1, A2, A3, A4, A5, A6, OP0, OP1, SB   (lsb, or 1st bit out)
**
*/

#define EPROM_EWEN      0x0019  /* Erase/Write enable (reversed) */
#define EPROM_EWDS      0x0001  /* Erase/Write disable (reversed) */
#define EPROM_READ      0x0003  /* Read (reversed) */
#define EPROM_WRITE     0x0005  /* Write (reversed) */
#define EPROM_ERASE     0x0007  /* Erase (reversed) */
#define EPROM_ERAL      0x0009  /* Erase All (reversed) */
#define EPROM_WRAL      0x0011  /* Write All (reversed) */

#define EPROM_ADR_SZ    7       /* Number of bits in offset address */
#define EPROM_OP_SZ     3       /* Number of bits in command */
#define SIZE_ADDR_OP    (EPROM_ADR_SZ + EPROM_OP_SZ)
#define LC46A_MAX_OPS   10      /* Number of bits in Instruction */
#define NUM_OF_BITS     8       /* Number of bits in data */


/* EEPROM signal bits */
#define EPROM_ACTIVE_OUT_BIT    0x0001  /* Out data bit */
#define EPROM_ACTIVE_IN_BIT     0x0002  /* In data bit */
#define ACTIVE_IN_BIT_SHIFT     0x0001  /* Shift In data bit to LSB */
#define EPROM_ENCS              0x0004  /* Set EEPROM CS during operation */


/*------------------------------------------------------------------------
 *      The ByteReverse table is used to reverses the 8 bits within a byte
 *------------------------------------------------------------------------
 */

static unsigned char ByteReverse[256];
static int  ByteReverseBuilt = FALSE;


/*------------------------------------------------------------------------
 *      mfg_template - initial serial EEPROM data structure
 *------------------------------------------------------------------------
 */

short       mfg_template[sizeof (FLD_TYPE2)] =
{
    PROM_FORMAT_TYPE2,          /* type; */
    0x00, 0x1A,                 /* length[2]; */
    0x00, 0x00, 0x00, 0x00,     /* Crc32[4]; */
    0x11, 0x76,                 /* Id[2]; */
    0x07, 0x05,                 /* SubId[2] E1; */
    0x00, 0xA0, 0xD6, 0x00, 0x00, 0x00, /* Serial[6]; */
    0x00, 0x00, 0x00, 0x00,     /* CreateTime[4]; */
    0x00, 0x00, 0x00, 0x00,     /* HeatRunTime[4]; */
    0x00, 0x00, 0x00, 0x00,     /* HeatRunIterations[4]; */
    0x00, 0x00, 0x00, 0x00,     /* HeatRunErrors[4]; */
};


/*------------------------------------------------------------------------
 *      BuildByteReverse - build the 8-bit reverse table
 *------------------------------------------------------------------------
 *
 *      The 'ByteReverse' table reverses the 8 bits within a byte
 *      (the MSB becomes the LSB etc.).
 */

static void
BuildByteReverse (void)
{
    long        half;           /* Used to build by powers to 2 */
    int         i;

    ByteReverse[0] = 0;

    for (half = 1; half < sizeof (ByteReverse); half <<= 1)
        for (i = 0; i < half; i++)
            ByteReverse[half + i] = (char) (ByteReverse[i] | (0x80 / half));

    ByteReverseBuilt = TRUE;
}


/*------------------------------------------------------------------------
 *      eeprom_delay - small delay for EEPROM timing
 *------------------------------------------------------------------------
 */

static void
eeprom_delay (void)
{
    int         timeout;

    for (timeout = 20; timeout; --timeout)
    {
        OS_uwait_dummy ();
    }
}


/*------------------------------------------------------------------------
 *      eeprom_put_byte - Send a byte to the EEPROM serially
 *------------------------------------------------------------------------
 *
 *      Given the PCI address and the data, this routine serially sends
 *      the data to the EEPROM.
 */

void
eeprom_put_byte (long addr, long data, int count)
{
    u_int32_t output;

    while (--count >= 0)
    {
        output = (data & EPROM_ACTIVE_OUT_BIT) ? 1 : 0; /* Get next data bit */
        output |= EPROM_ENCS;       /* Add Chip Select */
        data >>= 1;

        eeprom_delay ();
        pci_write_32 ((u_int32_t *) addr, output);      /* Output it */
    }
}


/*------------------------------------------------------------------------
 *      eeprom_get_byte - Receive a byte from the EEPROM serially
 *------------------------------------------------------------------------
 *
 *      Given the PCI address, this routine serially fetches the data
 *      from the  EEPROM.
 */

u_int32_t
eeprom_get_byte (long addr)
{
    u_int32_t   input;
    u_int32_t   data;
    int         count;

/*  Start the Reading of DATA
**
**  The first read is a dummy as the data is latched in the
**  EPLD and read on the next read access to the EEPROM.
*/

    input = pci_read_32 ((u_int32_t *) addr);

    data = 0;
    count = NUM_OF_BITS;
    while (--count >= 0)
    {
        eeprom_delay ();
        input = pci_read_32 ((u_int32_t *) addr);

        data <<= 1;                 /* Shift data over */
        data |= (input & EPROM_ACTIVE_IN_BIT) ? 1 : 0;

    }

    return data;
}


/*------------------------------------------------------------------------
 *      disable_pmc_eeprom - Disable writes to the EEPROM
 *------------------------------------------------------------------------
 *
 *      Issue the EEPROM command to disable writes.
 */

static void
disable_pmc_eeprom (long addr)
{
    eeprom_put_byte (addr, EPROM_EWDS, SIZE_ADDR_OP);

    pci_write_32 ((u_int32_t *) addr, 0);       /* this removes Chip Select
                                                 * from EEPROM */
}


/*------------------------------------------------------------------------
 *      enable_pmc_eeprom - Enable writes to the EEPROM
 *------------------------------------------------------------------------
 *
 *      Issue the EEPROM command to enable writes.
 */

static void
enable_pmc_eeprom (long addr)
{
    eeprom_put_byte (addr, EPROM_EWEN, SIZE_ADDR_OP);

    pci_write_32 ((u_int32_t *) addr, 0);       /* this removes Chip Select
                                                 * from EEPROM */
}


/*------------------------------------------------------------------------
 *      pmc_eeprom_read - EEPROM location read
 *------------------------------------------------------------------------
 *
 *      Given a EEPROM PCI address and location offset, this routine returns
 *      the contents of the specified location to the calling routine.
 */

u_int32_t
pmc_eeprom_read (long addr, long mem_offset)
{
    u_int32_t   data;           /* Data from chip */

    if (!ByteReverseBuilt)
        BuildByteReverse ();

    mem_offset = ByteReverse[0x7F & mem_offset];        /* Reverse address */
    /*
     * NOTE: The max offset address is 128 or half the reversal table. So the
     * LSB is always zero and counts as a built in shift of one bit.  So even
     * though we need to shift 3 bits to make room for the command, we only
     * need to shift twice more because of the built in shift.
     */
    mem_offset <<= 2;               /* Shift for command */
    mem_offset |= EPROM_READ;       /* Add command */

    eeprom_put_byte (addr, mem_offset, SIZE_ADDR_OP);   /* Output chip address */

    data = eeprom_get_byte (addr);  /* Read chip data */

    pci_write_32 ((u_int32_t *) addr, 0);       /* Remove Chip Select from
                                                 * EEPROM */

    return (data & 0x000000FF);
}


/*------------------------------------------------------------------------
 *      pmc_eeprom_write - EEPROM location write
 *------------------------------------------------------------------------
 *
 *      Given a EEPROM PCI address, location offset and value, this
 *      routine writes the value to the specified location.
 *
 *      Note: it is up to the caller to determine if the write
 *      operation succeeded.
 */

int
pmc_eeprom_write (long addr, long mem_offset, u_int32_t data)
{
    volatile u_int32_t temp;
    int         count;

    if (!ByteReverseBuilt)
        BuildByteReverse ();

    mem_offset = ByteReverse[0x7F & mem_offset];        /* Reverse address */
    /*
     * NOTE: The max offset address is 128 or half the reversal table. So the
     * LSB is always zero and counts as a built in shift of one bit.  So even
     * though we need to shift 3 bits to make room for the command, we only
     * need to shift twice more because of the built in shift.
     */
    mem_offset <<= 2;               /* Shift for command */
    mem_offset |= EPROM_WRITE;      /* Add command */

    eeprom_put_byte (addr, mem_offset, SIZE_ADDR_OP);   /* Output chip address */

    data = ByteReverse[0xFF & data];/* Reverse data */
    eeprom_put_byte (addr, data, NUM_OF_BITS);  /* Output chip data */

    pci_write_32 ((u_int32_t *) addr, 0);       /* Remove Chip Select from
                                                 * EEPROM */

/*
**  Must see Data In at a low state before completing this transaction.
**
**  Afterwards, the data bit will return to a high state, ~6 ms, terminating
**  the operation.
*/
    pci_write_32 ((u_int32_t *) addr, EPROM_ENCS);      /* Re-enable Chip Select */
    temp = pci_read_32 ((u_int32_t *) addr);    /* discard first read */
    temp = pci_read_32 ((u_int32_t *) addr);
    if (temp & EPROM_ACTIVE_IN_BIT)
    {
        temp = pci_read_32 ((u_int32_t *) addr);
        if (temp & EPROM_ACTIVE_IN_BIT)
        {
            pci_write_32 ((u_int32_t *) addr, 0);       /* Remove Chip Select
                                                         * from EEPROM */
            return (1);
        }
    }
    count = 1000;
    while (count--)
    {
        for (temp = 0; temp < 0x10; temp++)
            OS_uwait_dummy ();

        if (pci_read_32 ((u_int32_t *) addr) & EPROM_ACTIVE_IN_BIT)
            break;
    }

    if (count == -1)
        return (2);

    return (0);
}


/*------------------------------------------------------------------------
 *      pmcGetBuffValue - read the specified value from buffer
 *------------------------------------------------------------------------
 */

long
pmcGetBuffValue (char *ptr, int size)
{
    long        value = 0;
    int         index;

    for (index = 0; index < size; ++index)
    {
        value <<= 8;
        value |= ptr[index] & 0xFF;
    }

    return value;
}


/*------------------------------------------------------------------------
 *      pmcSetBuffValue - save the specified value to buffer
 *------------------------------------------------------------------------
 */

void
pmcSetBuffValue (char *ptr, long value, int size)
{
    int         index = size;

    while (--index >= 0)
    {
        ptr[index] = (char) (value & 0xFF);
        value >>= 8;
    }
}


/*------------------------------------------------------------------------
 *      pmc_eeprom_read_buffer - read EEPROM data into specified buffer
 *------------------------------------------------------------------------
 */

void
pmc_eeprom_read_buffer (long addr, long mem_offset, char *dest_ptr, int size)
{
    while (--size >= 0)
        *dest_ptr++ = (char) pmc_eeprom_read (addr, mem_offset++);
}


/*------------------------------------------------------------------------
 *      pmc_eeprom_write_buffer - write EEPROM data from specified buffer
 *------------------------------------------------------------------------
 */

void
pmc_eeprom_write_buffer (long addr, long mem_offset, char *dest_ptr, int size)
{
    enable_pmc_eeprom (addr);

    while (--size >= 0)
        pmc_eeprom_write (addr, mem_offset++, *dest_ptr++);

    disable_pmc_eeprom (addr);
}


/*------------------------------------------------------------------------
 *      pmcCalcCrc - calculate the CRC for the serial EEPROM structure
 *------------------------------------------------------------------------
 */

u_int32_t
pmcCalcCrc_T01 (void *bufp)
{
    FLD_TYPE2  *buf = bufp;
    u_int32_t   crc;            /* CRC of the structure */

    /* Calc CRC for type and length fields */
    sbeCrc (
            (u_int8_t *) &buf->type,
            (u_int32_t) STRUCT_OFFSET (FLD_TYPE1, Crc32),
            (u_int32_t) 0,
            (u_int32_t *) &crc);

#ifdef EEPROM_TYPE_DEBUG
    pr_info("sbeCrc: crc 1 calculated as %08x\n", crc); /* RLD DEBUG */
#endif
    return ~crc;
}

u_int32_t
pmcCalcCrc_T02 (void *bufp)
{
    FLD_TYPE2  *buf = bufp;
    u_int32_t   crc;            /* CRC of the structure */

    /* Calc CRC for type and length fields */
    sbeCrc (
            (u_int8_t *) &buf->type,
            (u_int32_t) STRUCT_OFFSET (FLD_TYPE2, Crc32),
            (u_int32_t) 0,
            (u_int32_t *) &crc);

    /* Calc CRC for remaining fields */
    sbeCrc (
            (u_int8_t *) &buf->Id[0],
            (u_int32_t) (sizeof (FLD_TYPE2) - STRUCT_OFFSET (FLD_TYPE2, Id)),
            (u_int32_t) crc,
            (u_int32_t *) &crc);

#ifdef EEPROM_TYPE_DEBUG
    pr_info("sbeCrc: crc 2 calculated as %08x\n", crc); /* RLD DEBUG */
#endif
    return crc;
}


/*------------------------------------------------------------------------
 *      pmc_init_seeprom - initialize the serial EEPROM structure
 *------------------------------------------------------------------------
 *
 *      At the front of the serial EEPROM there is a record that contains
 *      manufacturing information.  If the info does not already exist, it
 *      is created.  The only field modifiable by the operator is the
 *      serial number field.
 */

void
pmc_init_seeprom (u_int32_t addr, u_int32_t serialNum)
{
    PROMFORMAT  buffer;         /* Memory image of structure */
    u_int32_t   crc;            /* CRC of structure */
    time_t      createTime;
    int         i;

    createTime = get_seconds ();

    /* use template data */
    for (i = 0; i < sizeof (FLD_TYPE2); ++i)
        buffer.bytes[i] = mfg_template[i];

    /* Update serial number field in buffer */
    pmcSetBuffValue (&buffer.fldType2.Serial[3], serialNum, 3);

    /* Update create time field in buffer */
    pmcSetBuffValue (&buffer.fldType2.CreateTime[0], createTime, 4);

    /* Update CRC field in buffer */
    crc = pmcCalcCrc_T02 (&buffer);
    pmcSetBuffValue (&buffer.fldType2.Crc32[0], crc, 4);

#ifdef DEBUG
    for (i = 0; i < sizeof (FLD_TYPE2); ++i)
        pr_info("[%02X] = %02X\n", i, buffer.bytes[i] & 0xFF);
#endif

    /* Write structure to serial EEPROM */
    pmc_eeprom_write_buffer (addr, EE_MFG, (char *) &buffer, sizeof (FLD_TYPE2));
}


char
pmc_verify_cksum (void *bufp)
{
    FLD_TYPE1  *buf1 = bufp;
    FLD_TYPE2  *buf2 = bufp;
    u_int32_t   crc1, crc2;     /* CRC read from EEPROM */

    /* Retrieve contents of CRC field */
    crc1 = pmcGetBuffValue (&buf1->Crc32[0], sizeof (buf1->Crc32));
#ifdef EEPROM_TYPE_DEBUG
    pr_info("EEPROM: chksum 1 reads   as %08x\n", crc1);        /* RLD DEBUG */
#endif
    if ((buf1->type == PROM_FORMAT_TYPE1) &&
        (pmcCalcCrc_T01 ((void *) buf1) == crc1))
        return PROM_FORMAT_TYPE1;   /* checksum type 1 verified */

    crc2 = pmcGetBuffValue (&buf2->Crc32[0], sizeof (buf2->Crc32));
#ifdef EEPROM_TYPE_DEBUG
    pr_info("EEPROM: chksum 2 reads   as %08x\n", crc2);        /* RLD DEBUG */
#endif
    if ((buf2->type == PROM_FORMAT_TYPE2) &&
        (pmcCalcCrc_T02 ((void *) buf2) == crc2))
        return PROM_FORMAT_TYPE2;   /* checksum type 2 verified */

    return PROM_FORMAT_Unk;         /* failed to validate */
}


/*** End-of-File ***/
