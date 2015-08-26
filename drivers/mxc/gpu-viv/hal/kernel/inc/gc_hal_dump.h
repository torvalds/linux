/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 Vivante Corporation
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
*    Copyright (C) 2014  Vivante Corporation
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


#ifndef __gc_hal_dump_h_
#define __gc_hal_dump_h_

#ifdef __cplusplus
extern "C" {
#endif

/*
**    FILE LAYOUT:
**
**        gcsDUMP_FILE structure
**
**        gcsDUMP_DATA frame
**            gcsDUMP_DATA or gcDUMP_DATA_SIZE records rendingring the frame
**            gctUINT8 data[length]
*/

#define gcvDUMP_FILE_SIGNATURE        gcmCC('g','c','D','B')

typedef struct _gcsDUMP_FILE
{
    gctUINT32           signature;    /* File signature */
    gctSIZE_T             length;        /* Length of file */
    gctUINT32             frames;        /* Number of frames in file */
}
gcsDUMP_FILE;

typedef enum _gceDUMP_TAG
{
    gcvTAG_SURFACE                  = gcmCC('s','u','r','f'),
    gcvTAG_FRAME                    = gcmCC('f','r','m',' '),
    gcvTAG_COMMAND                  = gcmCC('c','m','d',' '),
    gcvTAG_INDEX                    = gcmCC('i','n','d','x'),
    gcvTAG_STREAM                   = gcmCC('s','t','r','m'),
    gcvTAG_TEXTURE                  = gcmCC('t','e','x','t'),
    gcvTAG_RENDER_TARGET            = gcmCC('r','n','d','r'),
    gcvTAG_DEPTH                    = gcmCC('z','b','u','f'),
    gcvTAG_RESOLVE                  = gcmCC('r','s','l','v'),
    gcvTAG_DELETE                   = gcmCC('d','e','l',' '),
    gcvTAG_BUFOBJ                   = gcmCC('b','u','f','o'),
}
gceDUMP_TAG;

typedef struct _gcsDUMP_SURFACE
{
    gceDUMP_TAG            type;        /* Type of record. */
    gctUINT32             address;    /* Address of the surface. */
    gctINT16              width;        /* Width of surface. */
    gctINT16               height;        /* Height of surface. */
    gceSURF_FORMAT        format;        /* Surface pixel format. */
    gctSIZE_T            length;        /* Number of bytes inside the surface. */
}
gcsDUMP_SURFACE;

typedef struct _gcsDUMP_DATA
{
    gceDUMP_TAG             type;        /* Type of record. */
    gctSIZE_T             length;        /* Number of bytes of data. */
    gctUINT32             address;    /* Address for the data. */
}
gcsDUMP_DATA;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_dump_h_ */

