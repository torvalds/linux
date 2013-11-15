#ifndef _INC_PMCC4_H_
#define _INC_PMCC4_H_

/*-----------------------------------------------------------------------------
 * pmcc4.h -
 *
 * Copyright (C) 2005  SBE, Inc.
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
 *
 * For further information, contact via email: support@sbei.com
 * SBE, Inc.  San Ramon, California  U.S.A.
 *-----------------------------------------------------------------------------
 */

#include <linux/types.h>

typedef int status_t;

#define SBE_DRVR_FAIL     0
#define SBE_DRVR_SUCCESS  1

/********************/
/* PMCC4 memory Map */
/********************/

#define COMET_OFFSET(x) (0x80000+(x)*0x10000)
#define EEPROM_OFFSET   0xC0000
#define CPLD_OFFSET     0xD0000

    struct pmcc4_timeslot_param
    {
        u_int8_t    card;       /* the card number */
        u_int8_t    port;       /* the port number */
        u_int8_t    _reserved1;
        u_int8_t    _reserved2;

        /*
         * each byte in bitmask below represents one timeslot (bitmask[0] is
         * for timeslot 0 and so on), each bit in the byte selects timeslot
         * bits for this channel (0xff - whole timeslot, 0x7f - 56kbps mode)
         */
        u_int8_t    bitmask[32];
    };

    struct c4_musycc_param
    {
        u_int8_t    RWportnum;
                    u_int16_t offset;
        u_int32_t   value;
    };

/*Alarm values */
#define sbeE1RMAI      0x100
#define sbeYelAlm      0x04
#define sbeRedAlm      0x02
#define sbeAISAlm      0x01

#define sbeE1errSMF    0x02
#define sbeE1CRC       0x01

#ifdef __KERNEL__

/*
 * Device Driver interface, routines are for internal use only.
 */

#include "pmcc4_private.h"

char       *get_hdlc_name (hdlc_device *);

/*
 * external interface
 */

void        c4_cleanup (void);
status_t    c4_chan_up (ci_t *, int channum);
status_t    c4_del_chan_stats (int channum);
status_t    c4_del_chan (int channum);
status_t    c4_get_iidinfo (ci_t *ci, struct sbe_iid_info *iip);
int         c4_is_chan_up (int channum);

void       *getuserbychan (int channum);
void        pci_flush_write (ci_t *ci);
void        sbecom_set_loglevel (int debuglevel);
char       *sbeid_get_bdname (ci_t *ci);
void        sbeid_set_bdtype (ci_t *ci);
void        sbeid_set_hdwbid (ci_t *ci);
u_int32_t   sbeCrc (u_int8_t *, u_int32_t, u_int32_t, u_int32_t *);

void        VMETRO_TRACE (void *);       /* put data into 8 LEDs */
void        VMETRO_TRIGGER (ci_t *, int);       /* Note: int = 0(default)
                                                 * thru 15 */

#if defined (SBE_ISR_TASKLET)
void        musycc_intr_bh_tasklet (ci_t *);

#endif

#endif                          /*** __KERNEL __ ***/
#endif                          /* _INC_PMCC4_H_ */
