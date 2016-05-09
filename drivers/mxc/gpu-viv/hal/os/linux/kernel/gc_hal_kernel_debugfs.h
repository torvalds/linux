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


#include <stdarg.h>

#ifndef __gc_hal_kernel_debugfs_h_
#define __gc_hal_kernel_debugfs_h_

 #define MAX_LINE_SIZE 768           /* Max bytes for a line of debug info */


 typedef struct _gcsDEBUGFS_Node gcsDEBUGFS_Node;

typedef struct _gcsDEBUGFS_DIR *gckDEBUGFS_DIR;
typedef struct _gcsDEBUGFS_DIR
{
    struct dentry *     root;
    struct list_head    nodeList;
}
gcsDEBUGFS_DIR;

typedef struct _gcsINFO
{
    const char *        name;
    int                 (*show)(struct seq_file*, void*);
    int                 (*write)(const char __user *buf, size_t count, void*);
}
gcsINFO;

typedef struct _gcsINFO_NODE
{
    gcsINFO *          info;
    gctPOINTER         device;
    struct dentry *    entry;
    struct list_head   head;
}
gcsINFO_NODE;

gceSTATUS
gckDEBUGFS_DIR_Init(
    IN gckDEBUGFS_DIR Dir,
    IN struct dentry *root,
    IN gctCONST_STRING Name
    );

gceSTATUS
gckDEBUGFS_DIR_CreateFiles(
    IN gckDEBUGFS_DIR Dir,
    IN gcsINFO * List,
    IN int count,
    IN gctPOINTER Data
    );

gceSTATUS
gckDEBUGFS_DIR_RemoveFiles(
    IN gckDEBUGFS_DIR Dir,
    IN gcsINFO * List,
    IN int count
    );

void
gckDEBUGFS_DIR_Deinit(
    IN gckDEBUGFS_DIR Dir
    );

/*******************************************************************************
 **
 **                             System Related
 **
 *******************************************************************************/

gctINT gckDEBUGFS_IsEnabled(void);

gctINT gckDEBUGFS_Initialize(void);

gctINT gckDEBUGFS_Terminate(void);


/*******************************************************************************
 **
 **                             Node Related
 **
 *******************************************************************************/

gctINT
gckDEBUGFS_CreateNode(
    IN gctPOINTER Device,
    IN gctINT SizeInKB,
    IN struct dentry * Root,
    IN gctCONST_STRING NodeName,
    OUT gcsDEBUGFS_Node **Node
    );

void gckDEBUGFS_FreeNode(
            IN gcsDEBUGFS_Node  * Node
            );



void gckDEBUGFS_SetCurrentNode(
            IN gcsDEBUGFS_Node  * Node
            );



void gckDEBUGFS_GetCurrentNode(
            OUT gcsDEBUGFS_Node  ** Node
            );


ssize_t gckDEBUGFS_Print(
                IN gctCONST_STRING  Message,
                ...
                );

#endif


