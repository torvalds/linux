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


#ifdef MODULE
#include <linux/module.h>
#endif
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <linux/completion.h>
#include <linux/seq_file.h>
#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_debug.h"

/*
   Prequsite:

   1) Debugfs feature must be enabled in the kernel.
       1.a) You can enable this, in the compilation of the uImage, all you have to do is, In the "make menuconfig" part,
       you have to enable the debugfs in the kernel hacking part of the menu.

   HOW TO USE:
   1) insert the driver with the following option logFileSize, Ex: insmod galcore.ko ...... logFileSize=10240
   This gives a circular buffer of 10 MB

   2)Usually after inserting the driver, the debug file system is mounted under /sys/kernel/debug/

        2.a)If the debugfs is not mounted, you must do "mount -t debugfs none /sys/kernel/debug"

   3) To read what is being printed in the debugfs file system:
        Ex : cat /sys/kernel/debug/gc/galcore_trace

   4)To write into the debug file system from user side :
        Ex: echo "hello" > cat /sys/kernel/debug/gc/galcore_trace

   5)To write into debugfs from kernel side, Use the function called gckDEBUGFS_Print

   How to Get Video Memory Usage:
   1) Select a process whose video memory usage can be dump, no need to reset it until <pid> is needed to be change.
        echo <pid>  > /sys/kernel/debug/gc/vidmem

   2) Get video memory usage.
        cat /sys/kernel/debug/gc/vidmem

   USECASE Kernel Dump:

   1) Go to /hal/inc/gc_hal_options.h, and enable the following flags:
        - #   define gcdDUMP                              1
        - #   define gcdDUMP_IN_KERNEL          1
        - #   define gcdDUMP_COMMAND          1

    2) Go to /hal/kernel/gc_hal_kernel_command.c and disable the following flag
        -#define gcdSIMPLE_COMMAND_DUMP  0

    3) Compile the driver
    4) insmod it with the logFileSize option
    5) Run an application
    6) You can get the dump by cat /sys/kernel/debug/gpu/galcore_trace

 */

/**/
typedef va_list gctDBGARGS ;
#define gcmkARGS_START(argument, pointer)   va_start(argument, pointer)
#define gcmkARGS_END(argument)                        va_end(argument)

#define gcmkDEBUGFS_PRINT(ArgumentSize, Message) \
  { \
      gctDBGARGS __arguments__; \
      gcmkARGS_START(__arguments__, Message); \
      _debugfs_res = _DebugFSPrint(ArgumentSize, Message, &__arguments__);\
      gcmkARGS_END(__arguments__); \
  }


gcmkDECLARE_LOCK(traceLock);

/* Debug File System Node Struct. */
struct _gcsDEBUGFS_Node
{
    /*wait queues for read and write operations*/
#if defined(DECLARE_WAIT_QUEUE_HEAD)
    wait_queue_head_t read_q , write_q ;
#else
    struct wait_queue *read_q , *write_q ;
#endif
    struct dentry *parent ; /*parent directory*/
    struct dentry *filen ; /*filename*/
    struct dentry *vidmem;
    struct semaphore sem ; /* mutual exclusion semaphore */
    char *data ; /* The circular buffer data */
    int size ; /* Size of the buffer pointed to by 'data' */
    int refcount ; /* Files that have this buffer open */
    int read_point ; /* Offset in circ. buffer of oldest data */
    int write_point ; /* Offset in circ. buffer of newest data */
    int offset ; /* Byte number of read_point in the stream */
    struct _gcsDEBUGFS_Node *next ;

    caddr_t temp;
    int tempSize;
};

/* amount of data in the queue */
#define gcmkNODE_QLEN(node) ( (node)->write_point >= (node)->read_point ? \
         (node)->write_point - (node)->read_point : \
         (node)->size - (node)->read_point + (node)->write_point)

/* byte number of the last byte in the queue */
#define gcmkNODE_FIRST_EMPTY_BYTE(node) ((node)->offset + gcmkNODE_QLEN(node))

/*Synchronization primitives*/
#define gcmkNODE_READQ(node) (&((node)->read_q))
#define gcmkNODE_WRITEQ(node) (&((node)->write_q))
#define gcmkNODE_SEM(node) (&((node)->sem))

/*Utilities*/
#define gcmkMIN(x, y) ((x) < (y) ? (x) : y)

/*Debug File System Struct*/
typedef struct _gcsDEBUGFS_
{
    gcsDEBUGFS_Node* linkedlist ;
    gcsDEBUGFS_Node* currentNode ;
    int isInited ;
} gcsDEBUGFS_ ;

/*debug file system*/
static gcsDEBUGFS_ gc_dbgfs ;

static int gc_debugfs_open(struct inode *inode, struct file *file)
{
    gcsINFO_NODE *node = inode->i_private;

    return single_open(file, node->info->show, node);
}

static const struct file_operations gc_debugfs_operations = {
    .owner = THIS_MODULE,
    .open = gc_debugfs_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

gceSTATUS
gckDEBUGFS_DIR_Init(
    IN gckDEBUGFS_DIR Dir,
    IN struct dentry *root,
    IN gctCONST_STRING Name
    )
{
    Dir->root = debugfs_create_dir(Name, root);

    if (!Dir->root)
    {
        return gcvSTATUS_NOT_SUPPORTED;
    }

    INIT_LIST_HEAD(&Dir->nodeList);

    return gcvSTATUS_OK;
}

gceSTATUS
gckDEBUGFS_DIR_CreateFiles(
    IN gckDEBUGFS_DIR Dir,
    IN gcsINFO * List,
    IN int count,
    IN gctPOINTER Data
    )
{
    int i;
    gcsINFO_NODE * node;
    gceSTATUS status;

    for (i = 0; i < count; i++)
    {
        /* Create a node. */
        node = (gcsINFO_NODE *)kzalloc(sizeof(gcsINFO_NODE), GFP_KERNEL);

        node->info   = &List[i];
        node->device = Data;

        /* Bind to a file. TODO: clean up when fail. */
        node->entry = debugfs_create_file(
            List[i].name, S_IRUGO|S_IWUSR, Dir->root, node, &gc_debugfs_operations);

        if (!node->entry)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        list_add(&(node->head), &(Dir->nodeList));
    }

    return gcvSTATUS_OK;

OnError:
    gcmkVERIFY_OK(gckDEBUGFS_DIR_RemoveFiles(Dir, List, count));
    return status;
}

gceSTATUS
gckDEBUGFS_DIR_RemoveFiles(
    IN gckDEBUGFS_DIR Dir,
    IN gcsINFO * List,
    IN int count
    )
{
    int i;
    gcsINFO_NODE * node;
    gcsINFO_NODE * temp;

    for (i = 0; i < count; i++)
    {
        list_for_each_entry_safe(node, temp, &Dir->nodeList, head)
        {
            if (node->info == &List[i])
            {
                debugfs_remove(node->entry);
                list_del(&node->head);
                kfree(node);
            }
        }
    }

    return gcvSTATUS_OK;
}

void
gckDEBUGFS_DIR_Deinit(
    IN gckDEBUGFS_DIR Dir
    )
{
    if (Dir->root != NULL)
    {
        debugfs_remove(Dir->root);
        Dir->root = NULL;
    }
}

/*******************************************************************************
 **
 **        READ & WRITE FUNCTIONS (START)
 **
 *******************************************************************************/

/*******************************************************************************
 **
 **  _ReadFromNode
 **
 **    1) reading bytes out of a circular buffer with wraparound.
 **    2)returns caddr_t, pointer to data read, which the caller must free.
 **    3) length is (a pointer to) the number of bytes to be read, which will be set by this function to
 **        be the number of bytes actually returned
 **
 *******************************************************************************/
static caddr_t
_ReadFromNode (
                gcsDEBUGFS_Node* Node ,
                size_t *Length ,
                loff_t *Offset
                )
{
    caddr_t retval ;
    int bytes_copied = 0 , n , start_point , remaining ;

    /* find the smaller of the total bytes we have available and what
     * the user is asking for */
    *Length = gcmkMIN ( *Length , gcmkNODE_QLEN(Node) ) ;

    remaining = * Length ;

    /* Get start point. */
    start_point = Node->read_point;

    /* allocate memory to return */
    if (remaining > Node->tempSize)
    {
        kfree(Node->temp);

        if ( ( retval = kmalloc ( sizeof (char ) * remaining , GFP_KERNEL ) ) == NULL )
            return NULL;

        Node->temp = retval;
        Node->tempSize = remaining;
    }
    else
    {
        retval = Node->temp;
    }

    /* copy the (possibly noncontiguous) data to our buffer */
    while ( remaining )
    {
        n = gcmkMIN ( remaining , Node->size - start_point ) ;
        memcpy ( retval + bytes_copied , Node->data + start_point , n ) ;
        bytes_copied += n ;
        remaining -= n ;
        start_point = ( start_point + n ) % Node->size ;
    }

    /* advance user's file pointer */
    Node->read_point = (Node->read_point + * Length) % Node->size ;

    return retval ;
}

/*******************************************************************************
 **
 **  _WriteToNode
 **
 ** 1) writes to a circular buffer with wraparound.
 ** 2)in case of an overflow, it overwrites the oldest unread data.
 **
 *********************************************************************************/
static void
_WriteToNode (
               gcsDEBUGFS_Node* Node ,
               caddr_t Buf ,
               int Length
               )
{
    int bytes_copied = 0 ;
    int overflow = 0 ;
    int n ;

    if ( Length + gcmkNODE_QLEN ( Node ) >= ( Node->size - 1 ) )
    {
        overflow = 1 ;
    }

    while ( Length )
    {
        /* how many contiguous bytes are available from the write point to
         * the end of the circular buffer? */
        n = gcmkMIN ( Length , Node->size - Node->write_point ) ;
        memcpy ( Node->data + Node->write_point , Buf + bytes_copied , n ) ;
        bytes_copied += n ;
        Length -= n ;
        Node->write_point = ( Node->write_point + n ) % Node->size ;
    }

    /* if there is an overflow, reset the read point to read whatever is
     * the oldest data that we have, that has not yet been
     * overwritten. */
    if ( overflow )
    {
        Node->read_point = ( Node->write_point + 1 ) % Node->size ;
    }
}

/*******************************************************************************
 **
 **         PRINTING UTILITY (START)
 **
 *******************************************************************************/

/*******************************************************************************
 **
 **  _GetArgumentSize
 **
 **
 *******************************************************************************/
static gctINT
_GetArgumentSize (
                   IN gctCONST_STRING Message
                   )
{
    gctINT i , count ;

    for ( i = 0 , count = 0 ; Message[i] ; i += 1 )
    {
        if ( Message[i] == '%' )
        {
            count += 1 ;
        }
    }
    return count * sizeof (unsigned int ) ;
}

/*******************************************************************************
 **
 ** _AppendString
 **
 **
 *******************************************************************************/
static ssize_t
_AppendString (
                IN gcsDEBUGFS_Node* Node ,
                IN gctCONST_STRING String ,
                IN int Length
                )
{
    int n ;

    /* if the message is longer than the buffer, just take the beginning
     * of it, in hopes that the reader (if any) will have time to read
     * before we wrap around and obliterate it */
    n = gcmkMIN ( Length , Node->size - 1 ) ;

    gcmkLOCKSECTION(traceLock);

    /* now copy it into the circular buffer and free our temp copy */
    _WriteToNode ( Node , (caddr_t)String , n ) ;

    gcmkUNLOCKSECTION(traceLock);

    return n ;
}

/*******************************************************************************
 **
 ** _DebugFSPrint
 **
 **
 *******************************************************************************/
static ssize_t
_DebugFSPrint (
                IN unsigned int ArgumentSize ,
                IN const char* Message ,
                IN gctDBGARGS * Arguments

                )
{
    char buffer[MAX_LINE_SIZE] ;
    int len ;
    ssize_t res=0;

    len = vsnprintf ( buffer , sizeof (buffer ) , Message , *( va_list * ) Arguments ) ;

    buffer[len] = '\0' ;

    /* Add end-of-line if missing. */
    if ( buffer[len - 1] != '\n' )
    {
        buffer[len ++] = '\n' ;
        buffer[len] = '\0' ;
    }

    res = _AppendString ( gc_dbgfs.currentNode , buffer , len ) ;

    wake_up_interruptible ( gcmkNODE_READQ ( gc_dbgfs.currentNode ) ) ; /* blocked in read*/

    return res;
}

/*******************************************************************************
 **
 **                     LINUX SYSTEM FUNCTIONS (START)
 **
 *******************************************************************************/
static int
_DebugFSOpen (
    struct inode* inode,
    struct file* filp
    )
{
    filp->private_data = inode->i_private;

    return 0;
}

/*******************************************************************************
 **
 **   _DebugFSRead
 **
 *******************************************************************************/
static ssize_t
_DebugFSRead (
               struct file *file ,
               char __user * buffer ,
               size_t length ,
               loff_t * offset
               )
{
    int retval ;
    caddr_t data_to_return ;
    gcsDEBUGFS_Node* node = file->private_data;

    if (node == NULL)
    {
        printk ( "debugfs_read: record not found\n" ) ;
        return - EIO ;
    }

    gcmkLOCKSECTION(traceLock);

    /* wait until there's data available (unless we do nonblocking reads) */
    while (!gcmkNODE_QLEN(node))
    {
        gcmkUNLOCKSECTION(traceLock);

        if ( file->f_flags & O_NONBLOCK )
        {
            return - EAGAIN ;
        }

        if ( wait_event_interruptible ( ( *( gcmkNODE_READQ ( node ) ) ) , ( *offset < gcmkNODE_FIRST_EMPTY_BYTE ( node ) ) ) )
        {
            return - ERESTARTSYS ; /* signal: tell the fs layer to handle it */
        }

        gcmkLOCKSECTION(traceLock);
    }

    data_to_return = _ReadFromNode ( node , &length , offset ) ;

    gcmkUNLOCKSECTION(traceLock);

    if ( data_to_return == NULL )
    {
        retval = 0 ;
        goto unlock ;
    }
    if ( copy_to_user ( buffer , data_to_return , length ) > 0 )
    {
        retval = - EFAULT ;
    }
    else
    {
        retval = length ;
    }
unlock:

    wake_up_interruptible ( gcmkNODE_WRITEQ ( node ) ) ;
    return retval ;
}

/*******************************************************************************
 **
 **_DebugFSWrite
 **
 *******************************************************************************/
static ssize_t
_DebugFSWrite (
                struct file *file ,
                const char __user * buffer ,
                size_t length ,
                loff_t * offset
                )
{
    caddr_t message = NULL ;
    int n ;
    gcsDEBUGFS_Node* node = file->private_data;

    /* get the metadata about this log */
    if (node == NULL)
    {
        return - EIO ;
    }

    if ( down_interruptible ( gcmkNODE_SEM ( node ) ) )
    {
        return - ERESTARTSYS ;
    }

    /* if the message is longer than the buffer, just take the beginning
     * of it, in hopes that the reader (if any) will have time to read
     * before we wrap around and obliterate it */
    n = gcmkMIN ( length , node->size - 1 ) ;

    /* make sure we have the memory for it */
    if ( ( message = kmalloc ( n , GFP_KERNEL ) ) == NULL )
    {
        up ( gcmkNODE_SEM ( node ) ) ;
        return - ENOMEM ;
    }


    /* copy into our temp buffer */
    if ( copy_from_user ( message , buffer , n ) > 0 )
    {
        up ( gcmkNODE_SEM ( node ) ) ;
        kfree ( message ) ;
        return - EFAULT ;
    }

    /* now copy it into the circular buffer and free our temp copy */
    _WriteToNode ( node , message , n ) ;

    kfree ( message ) ;
    up ( gcmkNODE_SEM ( node ) ) ;

    /* wake up any readers that might be waiting for the data.  we call
     * schedule in the vague hope that a reader will run before the
     * writer's next write, to avoid losing data. */
    wake_up_interruptible ( gcmkNODE_READQ ( node ) ) ;

    return n ;
}

int dumpProcess = 0;

void
_PrintCounter(
    struct seq_file *file,
    gcsDATABASE_COUNTERS * counter,
    gctCONST_STRING Name
    )
{
    seq_printf(file,"Counter: %s\n", Name);

    seq_printf(file,"%-9s%10s","", "All");

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Current");

    seq_printf(file,"%10lld", counter->bytes);

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Maximum");

    seq_printf(file,"%10lld", counter->maxBytes);

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Total");

    seq_printf(file,"%10lld", counter->totalBytes);

    seq_printf(file, "\n");
}

void
_ShowCounters(
    struct seq_file *file,
    gcsDATABASE_PTR database
    )
{
    gctUINT i = 0;
    gcsDATABASE_COUNTERS * counter;
    gcsDATABASE_COUNTERS * nonPaged;

    static gctCONST_STRING surfaceTypes[] = {
        "UNKNOWN",
        "Index",
        "Vertex",
        "Texture",
        "RT",
        "Depth",
        "Bitmap",
        "TS",
        "Image",
        "Mask",
        "Scissor",
        "HZDepth",
    };

    /* Get pointer to counters. */
    counter = &database->vidMem;

    nonPaged = &database->nonPaged;

    seq_printf(file,"Counter: vidMem (for each surface type)\n");

    seq_printf(file,"%-9s%10s","", "All");

    for (i = 1; i < gcvSURF_NUM_TYPES; i++)
    {
        counter = &database->vidMemType[i];

        seq_printf(file, "%10s",surfaceTypes[i]);
    }

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Current");

    seq_printf(file,"%10lld", database->vidMem.bytes);

    for (i = 1; i < gcvSURF_NUM_TYPES; i++)
    {
        counter = &database->vidMemType[i];

        seq_printf(file,"%10lld", counter->bytes);
    }

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Maximum");

    seq_printf(file,"%10lld", database->vidMem.maxBytes);

    for (i = 1; i < gcvSURF_NUM_TYPES; i++)
    {
        counter = &database->vidMemType[i];

        seq_printf(file,"%10lld", counter->maxBytes);
    }

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Total");

    seq_printf(file,"%10lld", database->vidMem.totalBytes);

    for (i = 1; i < gcvSURF_NUM_TYPES; i++)
    {
        counter = &database->vidMemType[i];

        seq_printf(file,"%10lld", counter->totalBytes);
    }

    seq_printf(file, "\n");

    seq_printf(file,"Counter: vidMem (for each pool)\n");

    seq_printf(file,"%-9s%10s","", "All");

    for (i = 1; i < gcvPOOL_NUMBER_OF_POOLS; i++)
    {
        seq_printf(file, "%10d", i);
    }

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Current");

    seq_printf(file,"%10lld", database->vidMem.bytes);

    for (i = 1; i < gcvPOOL_NUMBER_OF_POOLS; i++)
    {
        counter = &database->vidMemPool[i];

        seq_printf(file,"%10lld", counter->bytes);
    }

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Maximum");

    seq_printf(file,"%10lld", database->vidMem.maxBytes);

    for (i = 1; i < gcvPOOL_NUMBER_OF_POOLS; i++)
    {
        counter = &database->vidMemPool[i];

        seq_printf(file,"%10lld", counter->maxBytes);
    }

    seq_printf(file, "\n");

    seq_printf(file,"%-9s","Total");

    seq_printf(file,"%10lld", database->vidMem.totalBytes);

    for (i = 1; i < gcvPOOL_NUMBER_OF_POOLS; i++)
    {
        counter = &database->vidMemPool[i];

        seq_printf(file,"%10lld", counter->totalBytes);
    }

    seq_printf(file, "\n");

    /* Print nonPaged. */
    _PrintCounter(file, &database->nonPaged, "nonPaged");
    _PrintCounter(file, &database->contiguous, "contiguous");
    _PrintCounter(file, &database->mapUserMemory, "mapUserMemory");
    _PrintCounter(file, &database->mapMemory, "mapMemory");
}

static int vidmem_show(struct seq_file *file, void *unused)
{
    gceSTATUS status;
    gcsDATABASE_PTR database;
    gckGALDEVICE device = file->private;

    gckKERNEL kernel = _GetValidKernel(device);

    /* Find the database. */
    gcmkONERROR(
        gckKERNEL_FindDatabase(kernel, dumpProcess, gcvFALSE, &database));

    seq_printf(file, "VidMem Usage (Process %d):\n", dumpProcess);

    _ShowCounters(file, database);

    return 0;

OnError:
    return 0;
}

static int
vidmem_open(
    struct inode *inode,
    struct file *file
    )
{
    return single_open(file, vidmem_show, inode->i_private);
}

static ssize_t
vidmem_write(
    struct file *file,
    const char __user *buf,
    size_t count,
    loff_t *pos
    )
{
    dumpProcess = simple_strtol(buf, NULL, 0);
    return count;
}

/*******************************************************************************
 **
 ** File Operations Table
 **
 *******************************************************************************/
static const struct file_operations debugfs_operations = {
                                                          .owner = THIS_MODULE ,
                                                          .open = _DebugFSOpen ,
                                                          .read = _DebugFSRead ,
                                                          .write = _DebugFSWrite ,
} ;

static const struct file_operations vidmem_operations = {
    .owner = THIS_MODULE ,
    .open = vidmem_open,
    .read = seq_read,
    .write = vidmem_write,
    .llseek = seq_lseek,
    .release = single_release,
} ;

/*******************************************************************************
 **
 **                             INTERFACE FUNCTIONS (START)
 **
 *******************************************************************************/

/*******************************************************************************
 **
 **  gckDEBUGFS_IsEnabled
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/


gctINT
gckDEBUGFS_IsEnabled ( void )
{
    return gc_dbgfs.isInited ;
}
/*******************************************************************************
 **
 **  gckDEBUGFS_Initialize
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/

gctINT
gckDEBUGFS_Initialize ( void )
{
    if ( ! gc_dbgfs.isInited )
    {
        gc_dbgfs.linkedlist = gcvNULL ;
        gc_dbgfs.currentNode = gcvNULL ;
        gc_dbgfs.isInited = 1 ;
    }
    return gc_dbgfs.isInited ;
}
/*******************************************************************************
 **
 **  gckDEBUGFS_Terminate
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/

gctINT
gckDEBUGFS_Terminate ( void )
{
    gcsDEBUGFS_Node * next = gcvNULL ;
    gcsDEBUGFS_Node * temp = gcvNULL ;
    if ( gc_dbgfs.isInited )
    {
        temp = gc_dbgfs.linkedlist ;
        while ( temp != gcvNULL )
        {
            next = temp->next ;
            gckDEBUGFS_FreeNode ( temp ) ;
            kfree ( temp ) ;
            temp = next ;
        }
        gc_dbgfs.isInited = 0 ;
    }
    return 0 ;
}


/*******************************************************************************
 **
 **  gckDEBUGFS_CreateNode
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 **     gckDEBUGFS_FreeNode * Device
 **          Pointer to a variable receiving the gcsDEBUGFS_Node object pointer on
 **          success.
 *********************************************************************************/

gctINT
gckDEBUGFS_CreateNode (
    IN gctPOINTER Device,
    IN gctINT SizeInKB ,
    IN struct dentry * Root ,
    IN gctCONST_STRING NodeName ,
    OUT gcsDEBUGFS_Node **Node
    )
{
    gcsDEBUGFS_Node*node ;
    /* allocate space for our metadata and initialize it */
    if ( ( node = kmalloc ( sizeof (gcsDEBUGFS_Node ) , GFP_KERNEL ) ) == NULL )
        goto struct_malloc_failed ;

    /*Zero it out*/
    memset ( node , 0 , sizeof (gcsDEBUGFS_Node ) ) ;

    /*Init the sync primitives*/
#if defined(DECLARE_WAIT_QUEUE_HEAD)
    init_waitqueue_head ( gcmkNODE_READQ ( node ) ) ;
#else
    init_waitqueue ( gcmkNODE_READQ ( node ) ) ;
#endif

#if defined(DECLARE_WAIT_QUEUE_HEAD)
    init_waitqueue_head ( gcmkNODE_WRITEQ ( node ) ) ;
#else
    init_waitqueue ( gcmkNODE_WRITEQ ( node ) ) ;
#endif
    sema_init ( gcmkNODE_SEM ( node ) , 1 ) ;
    /*End the sync primitives*/

    /*creating the debug file system*/
    node->parent = Root;

    if (SizeInKB)
    {
        /* figure out how much of a buffer this should be and allocate the buffer */
        node->size = 1024 * SizeInKB ;
        if ( ( node->data = ( char * ) vmalloc ( sizeof (char ) * node->size ) ) == NULL )
            goto data_malloc_failed ;

        node->tempSize = 0;
        node->temp = NULL;

        /*creating the file*/
        node->filen = debugfs_create_file(NodeName, S_IRUGO|S_IWUSR, node->parent, node,
                                          &debugfs_operations);
    }

    node->vidmem
        = debugfs_create_file("vidmem", S_IRUGO|S_IWUSR, node->parent, Device, &vidmem_operations);

    /* add it to our linked list */
    node->next = gc_dbgfs.linkedlist ;
    gc_dbgfs.linkedlist = node ;


    /* pass the struct back */
    *Node = node ;
    return 0 ;


data_malloc_failed:
    kfree ( node ) ;
struct_malloc_failed:
    return - ENOMEM ;
}

/*******************************************************************************
 **
 **  gckDEBUGFS_FreeNode
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/
void
gckDEBUGFS_FreeNode (
                             IN gcsDEBUGFS_Node * Node
                             )
{

    gcsDEBUGFS_Node **ptr ;

    if ( Node == NULL )
    {
        printk ( "null passed to free_vinfo\n" ) ;
        return ;
    }

    down ( gcmkNODE_SEM ( Node ) ) ;
    /*free data*/
    vfree ( Node->data ) ;

    kfree(Node->temp);

    /*Close Debug fs*/
    if (Node->vidmem)
    {
        debugfs_remove(Node->vidmem);
    }

    if ( Node->filen )
    {
        debugfs_remove ( Node->filen ) ;
    }

    /* now delete the node from the linked list */
    ptr = & ( gc_dbgfs.linkedlist ) ;
    while ( *ptr != Node )
    {
        if ( ! *ptr )
        {
            printk ( "corrupt info list!\n" ) ;
            break ;
        }
        else
            ptr = & ( ( **ptr ).next ) ;
    }
    *ptr = Node->next ;
    up ( gcmkNODE_SEM ( Node ) ) ;
}

/*******************************************************************************
 **
 **   gckDEBUGFS_SetCurrentNode
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/
void
gckDEBUGFS_SetCurrentNode (
                                   IN gcsDEBUGFS_Node * Node
                                   )
{
    gc_dbgfs.currentNode = Node ;
}

/*******************************************************************************
 **
 **   gckDEBUGFS_GetCurrentNode
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/
void
gckDEBUGFS_GetCurrentNode (
                                   OUT gcsDEBUGFS_Node ** Node
                                   )
{
    *Node = gc_dbgfs.currentNode ;
}

/*******************************************************************************
 **
 **   gckDEBUGFS_Print
 **
 **
 **  INPUT:
 **
 **  OUTPUT:
 **
 *******************************************************************************/
ssize_t
gckDEBUGFS_Print (
                          IN gctCONST_STRING Message ,
                          ...
                          )
{
    ssize_t _debugfs_res;
    gcmkDEBUGFS_PRINT ( _GetArgumentSize ( Message ) , Message ) ;
    return _debugfs_res;
}
