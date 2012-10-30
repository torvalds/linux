/*
 * mbx_sh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Definitions for shared mailbox cmd/data values.(used on both
 * the GPP and DSP sides).
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 *  Bridge usage of OMAP mailbox 1 is determined by the "class" of the
 *  mailbox interrupt's cmd value received. The class value are defined
 *  as a bit (10 thru 15) being set.
 *
 *  Note: Only 16 bits of each is used. Other 16 bit data reg available.
 *
 *   16 bit Mbx bit defns:
 *
 * A). Exception/Error handling (Module DEH) : class = 0.
 *
 *    15         10                  0
 *   ---------------------------------
 *   |0|0|0|0|0|0|x|x|x|x|x|x|x|x|x|x|
 *   ---------------------------------
 *   |  (class)  | (module specific) |
 *
 *
 * B: DSP-DMA link driver channels (DDMA) : class = 1.
 *
 *    15         10                  0
 *   ---------------------------------
 *   |0|0|0|0|0|1|b|b|b|b|b|c|c|c|c|c|
 *   ---------------------------------
 *   |  (class)  | (module specific) |
 *
 *   where b -> buffer index  (32 DDMA buffers/chnl max)
 *         c -> channel Id    (32 DDMA chnls max)
 *
 *
 * C: Proc-copy link driver channels (PCPY) : class = 2.
 *
 *    15         10                  0
 *   ---------------------------------
 *   |0|0|0|0|1|0|x|x|x|x|x|x|x|x|x|x|
 *   ---------------------------------
 *   |  (class)  | (module specific) |
 *
 *
 * D: Zero-copy link driver channels (DDZC) : class = 4.
 *
 *    15         10                  0
 *   ---------------------------------
 *   |0|0|0|1|0|0|x|x|x|x|x|c|c|c|c|c|
 *   ---------------------------------
 *   |  (class)  | (module specific) |
 *
 *   where x -> not used
 *         c -> channel Id    (32 ZCPY chnls max)
 *
 *
 * E: Power management : class = 8.
 *
 *    15         10                  0
 *   ---------------------------------
 *   |0|0|1|0|0|0|x|x|x|x|x|c|c|c|c|c|

 * 	0010 00xx xxxc cccc
 *	0010 00nn pppp qqqq
 *	nn:
 *	00 = reserved
 *	01 = pwr state change
 *	10 = opp pre-change
 *	11 = opp post-change
 *
 *	if nn = pwr state change:
 *	pppp = don't care
 *	qqqq:
 *	0010 = hibernate
 *	0010 0001 0000 0010
 *	0110 = retention
 *	0010 0001 0000 0110
 *	others reserved
 *
 *	if nn = opp pre-change:
 *	pppp = current opp
 *	qqqq = next opp
 *
 *	if nn = opp post-change:
 *	pppp = prev opp
 *	qqqq = current opp
 *
 *   ---------------------------------
 *   |  (class)  | (module specific) |
 *
 *   where x -> not used
 *         c -> Power management command
 *
 */

#ifndef _MBX_SH_H
#define _MBX_SH_H

#define MBX_PCPY_CLASS     0x0800	/* PROC-COPY  " */
#define MBX_PM_CLASS       0x2000	/* Power Management */
#define MBX_DBG_CLASS      0x4000	/* For debugging purpose */

/*
 * Exception Handler codes
 * Magic code used to determine if DSP signaled exception.
 */
#define MBX_DEH_BASE        0x0
#define MBX_DEH_USERS_BASE  0x100	/* 256 */
#define MBX_DEH_LIMIT       0x3FF	/* 1023 */
#define MBX_DEH_RESET       0x101	/* DSP RESET (DEH) */

/*
 *  Link driver command/status codes.
 */

/*  Power Management Commands */
#define MBX_PM_DSPIDLE                  (MBX_PM_CLASS + 0x0)
#define MBX_PM_DSPWAKEUP                (MBX_PM_CLASS + 0x1)
#define MBX_PM_EMERGENCYSLEEP           (MBX_PM_CLASS + 0x2)
#define MBX_PM_SETPOINT_PRENOTIFY       (MBX_PM_CLASS + 0x6)
#define MBX_PM_SETPOINT_POSTNOTIFY      (MBX_PM_CLASS + 0x7)
#define MBX_PM_DSPRETENTION        (MBX_PM_CLASS + 0x8)
#define MBX_PM_DSPHIBERNATE        (MBX_PM_CLASS + 0x9)
#define MBX_PM_HIBERNATE_EN        (MBX_PM_CLASS + 0xA)
#define MBX_PM_OPP_REQ                  (MBX_PM_CLASS + 0xB)

/* Bridge Debug Commands */
#define MBX_DBG_SYSPRINTF       (MBX_DBG_CLASS + 0x0)

#endif /* _MBX_SH_H */
