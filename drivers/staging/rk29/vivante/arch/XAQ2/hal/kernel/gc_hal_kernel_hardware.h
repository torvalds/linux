/****************************************************************************
*
*    Copyright (C) 2005 - 2011 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_kernel_hardware_h_
#define __gc_hal_kernel_hardware_h_

#ifdef __cplusplus
extern "C" {
#endif

/* gckHARDWARE object. */
struct _gckHARDWARE
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gctKERNEL object. */
    gckKERNEL                   kernel;

    /* Pointer to gctOS object. */
    gckOS                       os;

    /* Chip characteristics. */
    gceCHIPMODEL                chipModel;
    gctUINT32                   chipRevision;
    gctUINT32                   chipFeatures;
    gctUINT32                   chipMinorFeatures0;
    gctUINT32                   chipMinorFeatures1;
    gctBOOL                     allowFastClear;
    gctBOOL                     allowCompression;
    gctUINT32                   powerBaseAddress;
    gctBOOL                     extraEventStates;

    gctUINT32                   streamCount;
    gctUINT32                   registerMax;
    gctUINT32                   threadCount;
    gctUINT32                   shaderCoreCount;
    gctUINT32                   vertexCacheSize;
    gctUINT32                   vertexOutputBufferSize;

    /* Big endian */
    gctBOOL                     bigEndian;

    /* Chip status */
    gctPOINTER                  powerMutex;
    gctUINT32                   powerProcess;
    gctUINT32                   powerThread;
    gceCHIPPOWERSTATE           chipPowerState;
    gctBOOL                     broadcast;
    gctBOOL                     settingPowerState;
    gctUINT32                   lastWaitLink;
};

gceSTATUS
gckHARDWARE_GetBaseAddress(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR BaseAddress
    );

gceSTATUS
gckHARDWARE_NeedBaseAddress(
    IN gckHARDWARE Hardware,
    IN gctUINT32 State,
    OUT gctBOOL_PTR NeedBase
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_hardware_h_ */

