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


#ifndef _gc_hal_kernel_mutex_h_
#define _gc_hal_kernel_mutex_h_

#include "gc_hal.h"
#include <linux/mutex.h>

/* Create a new mutex. */
#define gckOS_CreateMutex(Os, Mutex)                                \
({                                                                  \
    gceSTATUS _status;                                              \
    gcmkHEADER_ARG("Os=0x%X", Os);                                  \
                                                                    \
    /* Validate the arguments. */                                   \
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);                               \
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);                          \
                                                                    \
    /* Allocate the mutex structure. */                             \
    _status = gckOS_Allocate(Os, gcmSIZEOF(struct mutex), Mutex);   \
                                                                    \
    if (gcmIS_SUCCESS(_status))                                     \
    {                                                               \
        /* Initialize the mutex. */                                 \
        mutex_init(*(struct mutex **)Mutex);                        \
    }                                                               \
                                                                    \
    /* Return status. */                                            \
    gcmkFOOTER_ARG("*Mutex=0x%X", *(struct mutex **)Mutex);         \
    _status;                                                        \
})

#endif



