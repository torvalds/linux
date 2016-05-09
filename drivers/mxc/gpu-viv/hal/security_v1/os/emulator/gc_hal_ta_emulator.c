/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_ta.h"
#include "gc_hal_kernel_mutex.h"

#define _GC_OBJ_ZONE    gcvZONE_OS

gcTA globalTA = gcvNULL;
gctaOS globalTAos;

struct _gctaOS {
    void *os;

    gctPOINTER dispatchMutex;
};

gceSTATUS HALDECL
TAEmulator(
    void * Interface
    )
{
    gckOS_AcquireMutex(globalTAos->os, globalTAos->dispatchMutex, gcvINFINITE);

    gcTA_Dispatch(globalTA, Interface);

    gckOS_ReleaseMutex(globalTAos->os, globalTAos->dispatchMutex);
    return gcvSTATUS_OK;
}


gceSTATUS
gctaOS_ConstructOS(
    IN gckOS Os,
    OUT gctaOS *TAos
    )
{
    gctaOS os;
    gctPOINTER pointer;
    gceSTATUS status;

    gcmkONERROR(gckOS_AllocateMemory(Os, gcmSIZEOF(struct _gctaOS), &pointer));

    os = (gctaOS)pointer;
    os->os = Os;

    gcmkONERROR(gckOS_CreateMutex(Os, &os->dispatchMutex));

    gckOS_SetGPUPower(Os, gcvCORE_MAJOR, gcvTRUE, gcvTRUE);

    *TAos = globalTAos = os;

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gctaOS_DestroyOS(
    IN gctaOS Os
    )
{
    gckOS os = Os->os;

    gcmkVERIFY_OK(gckOS_DeleteMutex(os, Os->dispatchMutex));
    gcmkVERIFY_OK(gckOS_FreeMemory(os, Os));

    return gcvSTATUS_OK;
}

gceSTATUS
gctaOS_AllocateSecurityMemory(
    IN gctaOS Os,
    IN gctSIZE_T  *Bytes,
    OUT gctPOINTER *Logical,
    OUT gctPOINTER *Physical
    )
{
    gceSTATUS status;

    gcmkONERROR(gckOS_AllocateNonPagedMemory(Os->os, gcvFALSE, Bytes, (gctPHYS_ADDR *)Physical, Logical));

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gctaOS_FreeSecurityMemory(
    IN gctaOS Os,
    IN gctSIZE_T  Bytes,
    IN gctPOINTER Logical,
    OUT gctPOINTER Physical
    )
{
    gckOS_FreeNonPagedMemory(Os->os, Bytes, (gctPHYS_ADDR)Physical, Logical);
    return gcvSTATUS_OK;
}

gceSTATUS
gctaOS_AllocateNonSecurityMemory(
    IN gctaOS Os,
    IN gctSIZE_T  *Bytes,
    OUT gctPOINTER *Logical,
    OUT gctPOINTER *Physical
    )
{
    gceSTATUS status;

    gcmkONERROR(gckOS_AllocateNonPagedMemory(Os->os, gcvFALSE, Bytes, (gctPHYS_ADDR *)Physical, Logical));

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gctaOS_FreeNonSecurityMemory(
    IN gctaOS Os,
    IN gctSIZE_T  Bytes,
    IN gctPOINTER Logical,
    OUT gctPOINTER Physical
    )
{
    gckOS_FreeNonPagedMemory(Os->os, Bytes, (gctPHYS_ADDR)Physical, Logical);
    return gcvSTATUS_OK;
}

gceSTATUS
gctaOS_Allocate(
    IN gctUINT32 Bytes,
    OUT gctPOINTER *Pointer
    )
{
    return gckOS_AllocateMemory(globalTAos->os, Bytes, Pointer);
}

gceSTATUS
gctaOS_Free(
    IN gctPOINTER Pointer
    )
{
    return gckOS_FreeMemory(globalTAos->os, Pointer);
}

gceSTATUS
gctaOS_GetPhysicalAddress(
    IN gctaOS Os,
    IN gctPOINTER Logical,
    OUT gctPHYS_ADDR_T * Physical
    )
{
    gctPHYS_ADDR_T physical;
    gceSTATUS status;

    gcmkONERROR(gckOS_GetPhysicalAddress(Os->os, Logical, &physical));

    *Physical = (gctUINT32)physical;

    return gcvSTATUS_OK;

OnError:
    return status;
}

gceSTATUS
gctaOS_WriteRegister(
    IN gctaOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    )
{
    return gckOS_WriteRegister(Os->os, Address, Data);
}

gceSTATUS
gctaOS_ReadRegister(
    IN gctaOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 *Data
    )
{
    return gckOS_ReadRegister(Os->os, Address, Data);
}

gceSTATUS
gctaOS_MemCopy(
    IN gctUINT8_PTR Dest,
    IN gctUINT8_PTR Src,
    IN gctUINT32 Bytes
    )
{
    gckOS_MemCopy(Dest, Src, Bytes);
    return gcvSTATUS_OK;
}

gceSTATUS
gctaOS_ZeroMemory(
    IN gctUINT8_PTR Dest,
    IN gctUINT32 Bytes
    )
{
    gckOS_ZeroMemory(Dest, Bytes);
    return gcvSTATUS_OK;
}

void
gctaOS_CacheFlush(
    IN gctUINT8_PTR Dest,
    IN gctUINT32 Bytes
    )
{

}

void
gctaOS_CacheClean(
    IN gctUINT8_PTR Dest,
    IN gctUINT32 Bytes
    )
{

}

void
gctaOS_CacheInvalidate(
    IN gctUINT8_PTR Dest,
    IN gctUINT32 Bytes
    )
{

}

gceSTATUS
gctaOS_IsPhysicalSecure(
    IN gctaOS Os,
    IN gctUINT32 Physical,
    OUT gctBOOL *Secure
    )
{
    return gcvSTATUS_NOT_SUPPORTED;
}

gceSTATUS
gctaOS_Delay(
    IN gctaOS Os,
    IN gctUINT32 Delay
    )
{
    return gckOS_Delay(Os->os, Delay);
}


