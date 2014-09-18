/* visorchipset_umode.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/** @file *********************************************************************
 *
 *  This describes structures needed for the interface between the
 *  visorchipset driver and a user-mode component that opens the device.
 *
 ******************************************************************************
 */

#ifndef __VISORCHIPSET_UMODE_H
#define __VISORCHIPSET_UMODE_H



/** The user-mode program can access the control channel buffer directly
 *  via this memory map.
 */
#define VISORCHIPSET_MMAP_CONTROLCHANOFFSET    (0x00000000)
#define VISORCHIPSET_MMAP_CONTROLCHANSIZE      (0x00400000)  /* 4MB */

#endif /* __VISORCHIPSET_UMODE_H */
