/*
 * _cmm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Private header file defining CMM manager objects and defines needed
 * by IO manager to register shared memory regions when DSP base image
 * is loaded(bridge_io_on_loaded).
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _CMM_
#define _CMM_

/*
 *  These target side symbols define the beginning and ending addresses
 *  of the section of shared memory used for shared memory manager CMM.
 *  They are defined in the *cfg.cmd file by cdb code.
 */
#define SHM0_SHARED_BASE_SYM             "_SHM0_BEG"
#define SHM0_SHARED_END_SYM              "_SHM0_END"
#define SHM0_SHARED_RESERVED_BASE_SYM    "_SHM0_RSVDSTRT"

/*
 *  Shared Memory Region #0(SHMSEG0) is used in the following way:
 *
 *  |(_SHM0_BEG)                  | (_SHM0_RSVDSTRT)           | (_SHM0_END)
 *  V                             V                            V
 *  ------------------------------------------------------------
 *  |     DSP-side allocations    |    GPP-side allocations    |
 *  ------------------------------------------------------------
 *
 *
 */

#endif /* _CMM_ */
