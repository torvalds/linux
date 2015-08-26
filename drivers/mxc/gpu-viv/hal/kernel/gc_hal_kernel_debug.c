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


#include "gc_hal_kernel_precomp.h"
#include <gc_hal_kernel_debug.h>

/******************************************************************************\
******************************** Debug Variables *******************************
\******************************************************************************/

static gceSTATUS _lastError  = gcvSTATUS_OK;
static gctUINT32 _debugLevel = gcvLEVEL_ERROR;
/*
_debugZones config value
Please Reference define in gc_hal_base.h
*/
static gctUINT32 _debugZones = gcvZONE_NONE;

/******************************************************************************\
********************************* Debug Switches *******************************
\******************************************************************************/

/*
    gcdBUFFERED_OUTPUT

    When set to non-zero, all output is collected into a buffer with the
    specified size.  Once the buffer gets full, the debug buffer will be
    printed to the console. gcdBUFFERED_SIZE determines the size of the buffer.
*/
#define gcdBUFFERED_OUTPUT  0

/*
    gcdBUFFERED_SIZE

    When set to non-zero, all output is collected into a buffer with the
    specified size.  Once the buffer gets full, the debug buffer will be
    printed to the console.
*/
#define gcdBUFFERED_SIZE    (1024 * 1024 * 2)

/*
    gcdDMA_BUFFER_COUNT

    If greater then zero, the debugger will attempt to find the command buffer
    where DMA is currently executing and then print this buffer and
    (gcdDMA_BUFFER_COUNT - 1) buffers before the current one. If set to zero
    or the current buffer is not found, all buffers are printed.
*/
#define gcdDMA_BUFFER_COUNT 0

/*
    gcdTHREAD_BUFFERS

    When greater then one, will accumulate messages from the specified number
    of threads in separate output buffers.
*/
#define gcdTHREAD_BUFFERS   1

/*
    gcdENABLE_OVERFLOW

    When set to non-zero, and the output buffer gets full, instead of being
    printed, it will be allowed to overflow removing the oldest messages.
*/
#define gcdENABLE_OVERFLOW  1

/*
    gcdSHOW_LINE_NUMBER

    When enabledm each print statement will be preceeded with the current
    line number.
*/
#define gcdSHOW_LINE_NUMBER 0

/*
    gcdSHOW_PROCESS_ID

    When enabledm each print statement will be preceeded with the current
    process ID.
*/
#define gcdSHOW_PROCESS_ID  0

/*
    gcdSHOW_THREAD_ID

    When enabledm each print statement will be preceeded with the current
    thread ID.
*/
#define gcdSHOW_THREAD_ID   0

/*
    gcdSHOW_TIME

    When enabled each print statement will be preceeded with the current
    high-resolution time.
*/
#define gcdSHOW_TIME        0


/******************************************************************************\
****************************** Miscellaneous Macros ****************************
\******************************************************************************/

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
#   define gcmDBGASSERT(Expression, Format, Value) \
        if (!(Expression)) \
        { \
            _DirectPrint( \
                "*** gcmDBGASSERT ***************************\n" \
                "    function     : %s\n" \
                "    line         : %d\n" \
                "    expression   : " #Expression "\n" \
                "    actual value : " Format "\n", \
                __FUNCTION__, __LINE__, Value \
                ); \
        }
#else
#   define gcmDBGASSERT(Expression, Format, Value)
#endif

#define gcmPTRALIGNMENT(Pointer, Alignemnt) \
( \
    gcmALIGN(gcmPTR2INT32(Pointer), Alignemnt) - gcmPTR2INT32(Pointer) \
)

#if gcdALIGNBYSIZE
#   define gcmISALIGNED(Offset, Alignment) \
        (((Offset) & ((Alignment) - 1)) == 0)

#   define gcmkALIGNPTR(Type, Pointer, Alignment) \
        Pointer = (Type) gcmINT2PTR(gcmALIGN(gcmPTR2INT32(Pointer), Alignment))
#else
#   define gcmISALIGNED(Offset, Alignment) \
        gcvTRUE

#   define gcmkALIGNPTR(Type, Pointer, Alignment)
#endif

#define gcmALIGNSIZE(Offset, Size) \
    ((Size - Offset) + Size)

#define gcdHAVEPREFIX \
( \
       gcdSHOW_TIME \
    || gcdSHOW_LINE_NUMBER \
    || gcdSHOW_PROCESS_ID \
    || gcdSHOW_THREAD_ID \
)

#if gcdHAVEPREFIX

#   define gcdOFFSET                    0

#if gcdSHOW_TIME
#if gcmISALIGNED(gcdOFFSET, 8)
#           define gcdTIMESIZE          gcmSIZEOF(gctUINT64)
#       elif gcdOFFSET == 4
#           define gcdTIMESIZE          gcmALIGNSIZE(4, gcmSIZEOF(gctUINT64))
#       else
#           error "Unexpected offset value."
#       endif
#       undef  gcdOFFSET
#       define gcdOFFSET                8
#if !defined(gcdPREFIX_LEADER)
#           define gcdPREFIX_LEADER     gcmSIZEOF(gctUINT64)
#           define gcdTIMEFORMAT        "0x%016llX"
#       else
#           define gcdTIMEFORMAT        ", 0x%016llX"
#       endif
#   else
#       define gcdTIMESIZE              0
#       define gcdTIMEFORMAT
#   endif

#if gcdSHOW_LINE_NUMBER
#if gcmISALIGNED(gcdOFFSET, 8)
#           define gcdNUMSIZE           gcmSIZEOF(gctUINT64)
#       elif gcdOFFSET == 4
#           define gcdNUMSIZE           gcmALIGNSIZE(4, gcmSIZEOF(gctUINT64))
#       else
#           error "Unexpected offset value."
#       endif
#       undef  gcdOFFSET
#       define gcdOFFSET                8
#if !defined(gcdPREFIX_LEADER)
#           define gcdPREFIX_LEADER     gcmSIZEOF(gctUINT64)
#           define gcdNUMFORMAT         "%8llu"
#       else
#           define gcdNUMFORMAT         ", %8llu"
#       endif
#   else
#       define gcdNUMSIZE               0
#       define gcdNUMFORMAT
#   endif

#if gcdSHOW_PROCESS_ID
#if gcmISALIGNED(gcdOFFSET, 4)
#           define gcdPIDSIZE           gcmSIZEOF(gctUINT32)
#       else
#           error "Unexpected offset value."
#       endif
#       undef  gcdOFFSET
#       define gcdOFFSET                4
#if !defined(gcdPREFIX_LEADER)
#           define gcdPREFIX_LEADER     gcmSIZEOF(gctUINT32)
#           define gcdPIDFORMAT         "pid=%5d"
#       else
#           define gcdPIDFORMAT         ", pid=%5d"
#       endif
#   else
#       define gcdPIDSIZE               0
#       define gcdPIDFORMAT
#   endif

#if gcdSHOW_THREAD_ID
#if gcmISALIGNED(gcdOFFSET, 4)
#           define gcdTIDSIZE           gcmSIZEOF(gctUINT32)
#       else
#           error "Unexpected offset value."
#       endif
#       undef  gcdOFFSET
#       define gcdOFFSET                4
#if !defined(gcdPREFIX_LEADER)
#           define gcdPREFIX_LEADER     gcmSIZEOF(gctUINT32)
#           define gcdTIDFORMAT         "tid=%5d"
#       else
#           define gcdTIDFORMAT         ", tid=%5d"
#       endif
#   else
#       define gcdTIDSIZE               0
#       define gcdTIDFORMAT
#   endif

#   define gcdPREFIX_SIZE \
    ( \
          gcdTIMESIZE \
        + gcdNUMSIZE  \
        + gcdPIDSIZE  \
        + gcdTIDSIZE  \
    )

    static const char * _prefixFormat =
    "["
        gcdTIMEFORMAT
        gcdNUMFORMAT
        gcdPIDFORMAT
        gcdTIDFORMAT
    "] ";

#else

#   define gcdPREFIX_LEADER             gcmSIZEOF(gctUINT32)
#   define gcdPREFIX_SIZE               0

#endif

/* Assumed largest variable argument leader size. */
#define gcdVARARG_LEADER                gcmSIZEOF(gctUINT64)

/* Alignnments. */
#if gcdALIGNBYSIZE
#   define gcdPREFIX_ALIGNMENT gcdPREFIX_LEADER
#   define gcdVARARG_ALIGNMENT gcdVARARG_LEADER
#else
#   define gcdPREFIX_ALIGNMENT 0
#   define gcdVARARG_ALIGNMENT 0
#endif

#if gcdBUFFERED_OUTPUT
#   define gcdOUTPUTPREFIX _AppendPrefix
#   define gcdOUTPUTSTRING _AppendString
#   define gcdOUTPUTCOPY   _AppendCopy
#   define gcdOUTPUTBUFFER _AppendBuffer
#else
#   define gcdOUTPUTPREFIX _PrintPrefix
#   define gcdOUTPUTSTRING _PrintString
#   define gcdOUTPUTCOPY   _PrintString
#   define gcdOUTPUTBUFFER _PrintBuffer
#endif

/******************************************************************************\
****************************** Private Structures ******************************
\******************************************************************************/

typedef enum _gceBUFITEM
{
    gceBUFITEM_NONE,
    gcvBUFITEM_PREFIX,
    gcvBUFITEM_STRING,
    gcvBUFITEM_COPY,
    gcvBUFITEM_BUFFER
}
gceBUFITEM;

/* Common item head/buffer terminator. */
typedef struct _gcsBUFITEM_HEAD * gcsBUFITEM_HEAD_PTR;
typedef struct _gcsBUFITEM_HEAD
{
    gceBUFITEM              type;
}
gcsBUFITEM_HEAD;

/* String prefix (for ex. [     1,tid=0x019A]) */
typedef struct _gcsBUFITEM_PREFIX * gcsBUFITEM_PREFIX_PTR;
typedef struct _gcsBUFITEM_PREFIX
{
    gceBUFITEM              type;
#if gcdHAVEPREFIX
    gctPOINTER              prefixData;
#endif
}
gcsBUFITEM_PREFIX;

/* Buffered string. */
typedef struct _gcsBUFITEM_STRING * gcsBUFITEM_STRING_PTR;
typedef struct _gcsBUFITEM_STRING
{
    gceBUFITEM              type;
    gctINT                  indent;
    gctCONST_STRING         message;
    gctPOINTER              messageData;
    gctUINT                 messageDataSize;
}
gcsBUFITEM_STRING;

/* Buffered string (copy of the string is included with the record). */
typedef struct _gcsBUFITEM_COPY * gcsBUFITEM_COPY_PTR;
typedef struct _gcsBUFITEM_COPY
{
    gceBUFITEM              type;
    gctINT                  indent;
    gctPOINTER              messageData;
    gctUINT                 messageDataSize;
}
gcsBUFITEM_COPY;

/* Memory buffer. */
typedef struct _gcsBUFITEM_BUFFER * gcsBUFITEM_BUFFER_PTR;
typedef struct _gcsBUFITEM_BUFFER
{
    gceBUFITEM              type;
    gctINT                  indent;
    gceDUMP_BUFFER          bufferType;

#if gcdDMA_BUFFER_COUNT && (gcdTHREAD_BUFFERS == 1)
    gctUINT32               dmaAddress;
#endif

    gctUINT                 dataSize;
    gctUINT32               address;
#if gcdHAVEPREFIX
    gctPOINTER              prefixData;
#endif
}
gcsBUFITEM_BUFFER;

typedef struct _gcsBUFFERED_OUTPUT * gcsBUFFERED_OUTPUT_PTR;
typedef struct _gcsBUFFERED_OUTPUT
{
#if gcdTHREAD_BUFFERS > 1
    gctUINT32               threadID;
#endif

#if gcdSHOW_LINE_NUMBER
    gctUINT64               lineNumber;
#endif

    gctINT                  indent;

#if gcdBUFFERED_OUTPUT
    gctINT                  start;
    gctINT                  index;
    gctINT                  count;
    gctUINT8                buffer[gcdBUFFERED_SIZE];
#endif

    gcsBUFFERED_OUTPUT_PTR  prev;
    gcsBUFFERED_OUTPUT_PTR  next;
}
gcsBUFFERED_OUTPUT;

typedef gctUINT (* gcfPRINTSTRING) (
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gcsBUFITEM_HEAD_PTR Item
    );

typedef gctINT (* gcfGETITEMSIZE) (
    IN gcsBUFITEM_HEAD_PTR Item
    );

/******************************************************************************\
******************************* Private Variables ******************************
\******************************************************************************/

static gcsBUFFERED_OUTPUT     _outputBuffer[gcdTHREAD_BUFFERS];
static gcsBUFFERED_OUTPUT_PTR _outputBufferHead = gcvNULL;
static gcsBUFFERED_OUTPUT_PTR _outputBufferTail = gcvNULL;

/******************************************************************************\
****************************** Item Size Functions *****************************
\******************************************************************************/

#if gcdBUFFERED_OUTPUT
static gctINT
_GetTerminatorItemSize(
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
    return gcmSIZEOF(gcsBUFITEM_HEAD);
}

static gctINT
_GetPrefixItemSize(
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
#if gcdHAVEPREFIX
    gcsBUFITEM_PREFIX_PTR item = (gcsBUFITEM_PREFIX_PTR) Item;
    gctUINT vlen = ((gctUINT8_PTR) item->prefixData) - ((gctUINT8_PTR) item);
    return vlen + gcdPREFIX_SIZE;
#else
    return gcmSIZEOF(gcsBUFITEM_PREFIX);
#endif
}

static gctINT
_GetStringItemSize(
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
    gcsBUFITEM_STRING_PTR item = (gcsBUFITEM_STRING_PTR) Item;
    gctUINT vlen = ((gctUINT8_PTR) item->messageData) - ((gctUINT8_PTR) item);
    return vlen + item->messageDataSize;
}

static gctINT
_GetCopyItemSize(
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
    gcsBUFITEM_COPY_PTR item = (gcsBUFITEM_COPY_PTR) Item;
    gctUINT vlen = ((gctUINT8_PTR) item->messageData) - ((gctUINT8_PTR) item);
    return vlen + item->messageDataSize;
}

static gctINT
_GetBufferItemSize(
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
#if gcdHAVEPREFIX
    gcsBUFITEM_BUFFER_PTR item = (gcsBUFITEM_BUFFER_PTR) Item;
    gctUINT vlen = ((gctUINT8_PTR) item->prefixData) - ((gctUINT8_PTR) item);
    return vlen + gcdPREFIX_SIZE + item->dataSize;
#else
    gcsBUFITEM_BUFFER_PTR item = (gcsBUFITEM_BUFFER_PTR) Item;
    return gcmSIZEOF(gcsBUFITEM_BUFFER) + item->dataSize;
#endif
}

static gcfGETITEMSIZE _itemSize[] =
{
    _GetTerminatorItemSize,
    _GetPrefixItemSize,
    _GetStringItemSize,
    _GetCopyItemSize,
    _GetBufferItemSize
};
#endif

/******************************************************************************\
******************************* Printing Functions *****************************
\******************************************************************************/

#if gcdDEBUG || gcdBUFFERED_OUTPUT
static void
_DirectPrint(
    gctCONST_STRING Message,
    ...
    )
{
    gctINT len;
    char buffer[768];
    gctARGUMENTS arguments;

    gcmkARGUMENTS_START(arguments, Message);
    len = gcmkVSPRINTF(buffer, gcmSIZEOF(buffer), Message, &arguments);
    gcmkARGUMENTS_END(arguments);

    buffer[len] = '\0';
    gcmkOUTPUT_STRING(buffer);
}
#endif

static int
_AppendIndent(
    IN gctINT Indent,
    IN char * Buffer,
    IN int BufferSize
    )
{
    gctINT i;

    gctINT len    = 0;
    gctINT indent = Indent % 40;

    for (i = 0; i < indent; i += 1)
    {
        Buffer[len++] = ' ';
    }

    if (indent != Indent)
    {
        len += gcmkSPRINTF(
            Buffer + len, BufferSize - len, " <%d> ", Indent
            );

        Buffer[len] = '\0';
    }

    return len;
}

#if gcdHAVEPREFIX
static void
_PrintPrefix(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctPOINTER Data
    )
{
    char buffer[768];
    gctINT len;

    /* Format the string. */
    len = gcmkVSPRINTF(buffer, gcmSIZEOF(buffer), _prefixFormat, Data);
    buffer[len] = '\0';

    /* Print the string. */
    gcmkOUTPUT_STRING(buffer);
}
#endif

static void
_PrintString(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctINT Indent,
    IN gctCONST_STRING Message,
    IN gctUINT ArgumentSize,
    IN gctPOINTER Data
    )
{
    char buffer[768];
    gctINT len;

    /* Append the indent string. */
    len = _AppendIndent(Indent, buffer, gcmSIZEOF(buffer));

    /* Format the string. */
    len += gcmkVSPRINTF(buffer + len, gcmSIZEOF(buffer) - len, Message, Data);
    buffer[len] = '\0';

    /* Add end-of-line if missing. */
    if (buffer[len - 1] != '\n')
    {
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }

    /* Print the string. */
    gcmkOUTPUT_STRING(buffer);
}

static void
_PrintBuffer(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctINT Indent,
    IN gctPOINTER PrefixData,
    IN gctPOINTER Data,
    IN gctUINT Address,
    IN gctUINT DataSize,
    IN gceDUMP_BUFFER Type,
    IN gctUINT32 DmaAddress
    )
{
    static gctCONST_STRING _titleString[] =
    {
        "CONTEXT BUFFER",
        "USER COMMAND BUFFER",
        "KERNEL COMMAND BUFFER",
        "LINK BUFFER",
        "WAIT LINK BUFFER",
        ""
    };

    static const gctINT COLUMN_COUNT = 8;

    gctUINT i, count, column, address;
    gctUINT32_PTR data;
    gctCHAR buffer[768];
    gctUINT indent, len;
    gctBOOL command;

    /* Append space for the prefix. */
#if gcdHAVEPREFIX
    indent = gcmkVSPRINTF(buffer, gcmSIZEOF(buffer), _prefixFormat, PrefixData);
    buffer[indent] = '\0';
#else
    indent = 0;
#endif

    /* Append the indent string. */
    indent += _AppendIndent(
        Indent, buffer + indent, gcmSIZEOF(buffer) - indent
        );

    switch (Type)
    {
    case gceDUMP_BUFFER_CONTEXT:
    case gceDUMP_BUFFER_USER:
    case gceDUMP_BUFFER_KERNEL:
    case gceDUMP_BUFFER_LINK:
    case gceDUMP_BUFFER_WAITLINK:
        /* Form and print the title string. */
        gcmkSPRINTF2(
            buffer + indent, gcmSIZEOF(buffer) - indent,
            "%s%s\n", _titleString[Type],
            ((DmaAddress >= Address) && (DmaAddress < Address + DataSize))
                ? " (CURRENT)" : ""
            );

        gcmkOUTPUT_STRING(buffer);

        /* Terminate the string. */
        buffer[indent] = '\0';

        /* This is a command buffer. */
        command = gcvTRUE;
        break;

    case gceDUMP_BUFFER_FROM_USER:
        /* This is not a command buffer. */
        command = gcvFALSE;

        /* No title. */
        break;

    default:
        gcmDBGASSERT(gcvFALSE, "%s", "invalid buffer type");

        /* This is not a command buffer. */
        command = gcvFALSE;
    }

    /* Overwrite the prefix with spaces. */
    for (i = 0; i < indent; i += 1)
    {
        buffer[i] = ' ';
    }

    /* Form and print the opening string. */
    if (command)
    {
        gcmkSPRINTF2(
            buffer + indent, gcmSIZEOF(buffer) - indent,
            "@[kernel.command %08X %08X\n", Address, DataSize
            );

        gcmkOUTPUT_STRING(buffer);

        /* Terminate the string. */
        buffer[indent] = '\0';
    }

    /* Get initial address. */
    address = Address;

    /* Cast the data pointer. */
    data = (gctUINT32_PTR) Data;

    /* Compute the number of double words. */
    count = DataSize / gcmSIZEOF(gctUINT32);

    /* Print the buffer. */
    for (i = 0, len = indent, column = 0; i < count; i += 1)
    {
        /* Append the address. */
        if (column == 0)
        {
            len += gcmkSPRINTF(
                buffer + len, gcmSIZEOF(buffer) - len, "0x%08X:", address
                );
        }

        /* Append the data value. */
        len += gcmkSPRINTF2(
            buffer + len, gcmSIZEOF(buffer) - len, "%c%08X",
            (address == DmaAddress)? '>' : ' ', data[i]
            );

        buffer[len] = '\0';

        /* Update the address. */
        address += gcmSIZEOF(gctUINT32);

        /* Advance column count. */
        column += 1;

        /* End of line? */
        if ((column % COLUMN_COUNT) == 0)
        {
            /* Append EOL. */
            gcmkSTRCAT(buffer + len, gcmSIZEOF(buffer) - len, "\n");

            /* Print the string. */
            gcmkOUTPUT_STRING(buffer);

            /* Reset. */
            len    = indent;
            column = 0;
        }
    }

    /* Print the last partial string. */
    if (column != 0)
    {
        /* Append EOL. */
        gcmkSTRCAT(buffer + len, gcmSIZEOF(buffer) - len, "\n");

        /* Print the string. */
        gcmkOUTPUT_STRING(buffer);
    }

    /* Form and print the opening string. */
    if (command)
    {
        buffer[indent] = '\0';
        gcmkSTRCAT(buffer, gcmSIZEOF(buffer), "] -- command\n");
        gcmkOUTPUT_STRING(buffer);
    }
}

#if gcdBUFFERED_OUTPUT
static gctUINT
_PrintNone(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
    /* Return the size of the node. */
    return gcmSIZEOF(gcsBUFITEM_HEAD);
}

static gctUINT
_PrintPrefixWrapper(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
#if gcdHAVEPREFIX
    gcsBUFITEM_PREFIX_PTR item;
    gctUINT vlen;

    /* Get access to the data. */
    item = (gcsBUFITEM_PREFIX_PTR) Item;

    /* Print the message. */
    _PrintPrefix(OutputBuffer, item->prefixData);

    /* Compute the size of the variable portion of the structure. */
    vlen = ((gctUINT8_PTR) item->prefixData) - ((gctUINT8_PTR) item);

    /* Return the size of the node. */
    return vlen + gcdPREFIX_SIZE;
#else
    return gcmSIZEOF(gcsBUFITEM_PREFIX);
#endif
}

static gctUINT
_PrintStringWrapper(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
    gcsBUFITEM_STRING_PTR item;
    gctUINT vlen;

    /* Get access to the data. */
    item = (gcsBUFITEM_STRING_PTR) Item;

    /* Print the message. */
    _PrintString(
        OutputBuffer,
        item->indent, item->message, item->messageDataSize, item->messageData
        );

    /* Compute the size of the variable portion of the structure. */
    vlen = ((gctUINT8_PTR) item->messageData) - ((gctUINT8_PTR) item);

    /* Return the size of the node. */
    return vlen + item->messageDataSize;
}

static gctUINT
_PrintCopyWrapper(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
    gcsBUFITEM_COPY_PTR item;
    gctCONST_STRING message;
    gctUINT vlen;

    /* Get access to the data. */
    item = (gcsBUFITEM_COPY_PTR) Item;

    /* Determine the string pointer. */
    message = (gctCONST_STRING) (item + 1);

    /* Print the message. */
    _PrintString(
        OutputBuffer,
        item->indent, message, item->messageDataSize, item->messageData
        );

    /* Compute the size of the variable portion of the structure. */
    vlen = ((gctUINT8_PTR) item->messageData) - ((gctUINT8_PTR) item);

    /* Return the size of the node. */
    return vlen + item->messageDataSize;
}

static gctUINT
_PrintBufferWrapper(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gcsBUFITEM_HEAD_PTR Item
    )
{
#if gcdHAVEPREFIX
    gctUINT32 dmaAddress;
    gcsBUFITEM_BUFFER_PTR item;
    gctPOINTER data;
    gctUINT vlen;

    /* Get access to the data. */
    item = (gcsBUFITEM_BUFFER_PTR) Item;

#if gcdDMA_BUFFER_COUNT && (gcdTHREAD_BUFFERS == 1)
    dmaAddress = item->dmaAddress;
#else
    dmaAddress = 0xFFFFFFFF;
#endif

    if (dmaAddress != 0)
    {
        /* Compute the data address. */
        data = ((gctUINT8_PTR) item->prefixData) + gcdPREFIX_SIZE;

        /* Print buffer. */
        _PrintBuffer(
            OutputBuffer,
            item->indent, item->prefixData,
            data, item->address, item->dataSize,
            item->bufferType, dmaAddress
            );
    }

    /* Compute the size of the variable portion of the structure. */
    vlen = ((gctUINT8_PTR) item->prefixData) - ((gctUINT8_PTR) item);

    /* Return the size of the node. */
    return vlen + gcdPREFIX_SIZE + item->dataSize;
#else
    gctUINT32 dmaAddress;
    gcsBUFITEM_BUFFER_PTR item;

    /* Get access to the data. */
    item = (gcsBUFITEM_BUFFER_PTR) Item;

#if gcdDMA_BUFFER_COUNT && (gcdTHREAD_BUFFERS == 1)
    dmaAddress = item->dmaAddress;
#else
    dmaAddress = 0xFFFFFFFF;
#endif

    if (dmaAddress != 0)
    {
        /* Print buffer. */
        _PrintBuffer(
            OutputBuffer,
            item->indent, gcvNULL,
            item + 1, item->address, item->dataSize,
            item->bufferType, dmaAddress
            );
    }

    /* Return the size of the node. */
    return gcmSIZEOF(gcsBUFITEM_BUFFER) + item->dataSize;
#endif
}

static gcfPRINTSTRING _printArray[] =
{
    _PrintNone,
    _PrintPrefixWrapper,
    _PrintStringWrapper,
    _PrintCopyWrapper,
    _PrintBufferWrapper
};
#endif

/******************************************************************************\
******************************* Private Functions ******************************
\******************************************************************************/

#if gcdBUFFERED_OUTPUT

#if gcdDMA_BUFFER_COUNT && (gcdTHREAD_BUFFERS == 1)
static gcsBUFITEM_BUFFER_PTR
_FindCurrentDMABuffer(
    gctUINT32 DmaAddress
    )
{
    gctINT i, skip;
    gcsBUFITEM_HEAD_PTR item;
    gcsBUFITEM_BUFFER_PTR dmaCurrent;

    /* Reset the current buffer. */
    dmaCurrent = gcvNULL;

    /* Get the first stored item. */
    item = (gcsBUFITEM_HEAD_PTR) &_outputBufferHead->buffer[_outputBufferHead->start];

    /* Run through all items. */
    for (i = 0; i < _outputBufferHead->count; i += 1)
    {
        /* Buffer item? */
        if (item->type == gcvBUFITEM_BUFFER)
        {
            gcsBUFITEM_BUFFER_PTR buffer = (gcsBUFITEM_BUFFER_PTR) item;

            if ((DmaAddress >= buffer->address) &&
                (DmaAddress <  buffer->address + buffer->dataSize))
            {
                dmaCurrent = buffer;
            }
        }

        /* Get the item size and skip it. */
        skip = (* _itemSize[item->type]) (item);
        item = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + skip);

        /* End of the buffer? Wrap around. */
        if (item->type == gceBUFITEM_NONE)
        {
            item = (gcsBUFITEM_HEAD_PTR) _outputBufferHead->buffer;
        }
    }

    /* Return result. */
    return dmaCurrent;
}

static void
_EnableAllDMABuffers(
    void
    )
{
    gctINT i, skip;
    gcsBUFITEM_HEAD_PTR item;

    /* Get the first stored item. */
    item = (gcsBUFITEM_HEAD_PTR) &_outputBufferHead->buffer[_outputBufferHead->start];

    /* Run through all items. */
    for (i = 0; i < _outputBufferHead->count; i += 1)
    {
        /* Buffer item? */
        if (item->type == gcvBUFITEM_BUFFER)
        {
            gcsBUFITEM_BUFFER_PTR buffer = (gcsBUFITEM_BUFFER_PTR) item;

            /* Enable the buffer. */
            buffer->dmaAddress = ~0U;
        }

        /* Get the item size and skip it. */
        skip = (* _itemSize[item->type]) (item);
        item = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + skip);

        /* End of the buffer? Wrap around. */
        if (item->type == gceBUFITEM_NONE)
        {
            item = (gcsBUFITEM_HEAD_PTR) _outputBufferHead->buffer;
        }
    }
}

static void
_EnableDMABuffers(
    gctUINT32 DmaAddress,
    gcsBUFITEM_BUFFER_PTR CurrentDMABuffer
    )
{
    gctINT i, skip, index;
    gcsBUFITEM_HEAD_PTR item;
    gcsBUFITEM_BUFFER_PTR buffers[gcdDMA_BUFFER_COUNT];

    /* Reset buffer pointers. */
    gckOS_ZeroMemory(buffers, gcmSIZEOF(buffers));

    /* Set the current buffer index. */
    index = -1;

    /* Get the first stored item. */
    item = (gcsBUFITEM_HEAD_PTR) &_outputBufferHead->buffer[_outputBufferHead->start];

    /* Run through all items until the current DMA buffer is found. */
    for (i = 0; i < _outputBufferHead->count; i += 1)
    {
        /* Buffer item? */
        if (item->type == gcvBUFITEM_BUFFER)
        {
            /* Advance the index. */
            index = (index + 1) % gcdDMA_BUFFER_COUNT;

            /* Add to the buffer array. */
            buffers[index] = (gcsBUFITEM_BUFFER_PTR) item;

            /* Stop if this is the current DMA buffer. */
            if ((gcsBUFITEM_BUFFER_PTR) item == CurrentDMABuffer)
            {
                break;
            }
        }

        /* Get the item size and skip it. */
        skip = (* _itemSize[item->type]) (item);
        item = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + skip);

        /* End of the buffer? Wrap around. */
        if (item->type == gceBUFITEM_NONE)
        {
            item = (gcsBUFITEM_HEAD_PTR) _outputBufferHead->buffer;
        }
    }

    /* Enable the found buffers. */
    gcmDBGASSERT(index != -1, "%d", index);

    for (i = 0; i < gcdDMA_BUFFER_COUNT; i += 1)
    {
        if (buffers[index] == gcvNULL)
        {
            break;
        }

        buffers[index]->dmaAddress = DmaAddress;

        index -= 1;

        if (index == -1)
        {
            index = gcdDMA_BUFFER_COUNT - 1;
        }
    }
}
#endif

static void
_Flush(
    gctUINT32 DmaAddress
    )
{
    gctINT i, skip;
    gcsBUFITEM_HEAD_PTR item;

    gcsBUFFERED_OUTPUT_PTR outputBuffer = _outputBufferHead;

#if gcdDMA_BUFFER_COUNT && (gcdTHREAD_BUFFERS == 1)
    if ((outputBuffer != gcvNULL) && (outputBuffer->count != 0))
    {
        /* Find the current DMA buffer. */
        gcsBUFITEM_BUFFER_PTR dmaCurrent = _FindCurrentDMABuffer(DmaAddress);

        /* Was the current buffer found? */
        if (dmaCurrent == gcvNULL)
        {
            /* No, print all buffers. */
            _EnableAllDMABuffers();
        }
        else
        {
            /* Yes, enable only specified number of buffers. */
            _EnableDMABuffers(DmaAddress, dmaCurrent);
        }
    }
#endif

    while (outputBuffer != gcvNULL)
    {
        if (outputBuffer->count != 0)
        {
            _DirectPrint("********************************************************************************\n");
            _DirectPrint("FLUSHING DEBUG OUTPUT BUFFER (%d elements).\n", outputBuffer->count);
            _DirectPrint("********************************************************************************\n");

            item = (gcsBUFITEM_HEAD_PTR) &outputBuffer->buffer[outputBuffer->start];

            for (i = 0; i < outputBuffer->count; i += 1)
            {
                skip = (* _printArray[item->type]) (outputBuffer, item);

                item = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + skip);

                if (item->type == gceBUFITEM_NONE)
                {
                    item = (gcsBUFITEM_HEAD_PTR) outputBuffer->buffer;
                }
            }

            outputBuffer->start = 0;
            outputBuffer->index = 0;
            outputBuffer->count = 0;
        }

        outputBuffer = outputBuffer->next;
    }
}

static gcsBUFITEM_HEAD_PTR
_AllocateItem(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctINT Size
    )
{
    gctINT skip;
    gcsBUFITEM_HEAD_PTR item, next;

#if gcdENABLE_OVERFLOW
    if (
            (OutputBuffer->index + Size >= gcdBUFFERED_SIZE - gcmSIZEOF(gcsBUFITEM_HEAD))
            ||
            (
                (OutputBuffer->index        <  OutputBuffer->start) &&
                (OutputBuffer->index + Size >= OutputBuffer->start)
            )
    )
    {
        if (OutputBuffer->index + Size >= gcdBUFFERED_SIZE - gcmSIZEOF(gcsBUFITEM_HEAD))
        {
            if (OutputBuffer->index < OutputBuffer->start)
            {
                item = (gcsBUFITEM_HEAD_PTR) &OutputBuffer->buffer[OutputBuffer->start];

                while (item->type != gceBUFITEM_NONE)
                {
                    skip = (* _itemSize[item->type]) (item);

                    OutputBuffer->start += skip;
                    OutputBuffer->count -= 1;

                    item->type = gceBUFITEM_NONE;
                    item = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + skip);
                }

                OutputBuffer->start = 0;
            }

            OutputBuffer->index = 0;
        }

        item = (gcsBUFITEM_HEAD_PTR) &OutputBuffer->buffer[OutputBuffer->start];

        while (OutputBuffer->start - OutputBuffer->index <= Size)
        {
            skip = (* _itemSize[item->type]) (item);

            OutputBuffer->start += skip;
            OutputBuffer->count -= 1;

            item->type = gceBUFITEM_NONE;
            item = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + skip);

            if (item->type == gceBUFITEM_NONE)
            {
                OutputBuffer->start = 0;
                break;
            }
        }
    }
#else
    if (OutputBuffer->index + Size > gcdBUFFERED_SIZE - gcmSIZEOF(gcsBUFITEM_HEAD))
    {
        _DirectPrint("\nMessage buffer full; forcing message flush.\n\n");
        _Flush(~0U);
    }
#endif

    item = (gcsBUFITEM_HEAD_PTR) &OutputBuffer->buffer[OutputBuffer->index];

    OutputBuffer->index += Size;
    OutputBuffer->count += 1;

    next = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) item + Size);
    next->type = gceBUFITEM_NONE;

    return item;
}

#if gcdALIGNBYSIZE
static void
_FreeExtraSpace(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctPOINTER Item,
    IN gctINT ItemSize,
    IN gctINT FreeSize
    )
{
    gcsBUFITEM_HEAD_PTR next;

    OutputBuffer->index -= FreeSize;

    next = (gcsBUFITEM_HEAD_PTR) ((gctUINT8_PTR) Item + ItemSize);
    next->type = gceBUFITEM_NONE;
}
#endif

#if gcdHAVEPREFIX
static void
_AppendPrefix(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctPOINTER Data
    )
{
    gctUINT8_PTR prefixData;
    gcsBUFITEM_PREFIX_PTR item;
    gctINT allocSize;

#if gcdALIGNBYSIZE
    gctUINT alignment;
    gctINT size, freeSize;
#endif

    gcmDBGASSERT(Data != gcvNULL, "%p", Data);

    /* Determine the maximum item size. */
    allocSize
        = gcmSIZEOF(gcsBUFITEM_PREFIX)
        + gcdPREFIX_SIZE
        + gcdPREFIX_ALIGNMENT;

    /* Allocate prefix item. */
    item = (gcsBUFITEM_PREFIX_PTR) _AllocateItem(OutputBuffer, allocSize);

    /* Compute the initial prefix data pointer. */
    prefixData = (gctUINT8_PTR) (item + 1);

    /* Align the data pointer as necessary. */
#if gcdALIGNBYSIZE
    alignment = gcmPTRALIGNMENT(prefixData, gcdPREFIX_ALIGNMENT);
    prefixData += alignment;
#endif

    /* Set item data. */
    item->type       = gcvBUFITEM_PREFIX;
    item->prefixData = prefixData;

    /* Copy argument value. */
    gcmkMEMCPY(prefixData, Data, gcdPREFIX_SIZE);

#if gcdALIGNBYSIZE
    /* Compute the actual node size. */
    size = gcmSIZEOF(gcsBUFITEM_PREFIX) + gcdPREFIX_SIZE + alignment;

    /* Free extra memory if any. */
    freeSize = allocSize - size;
    if (freeSize != 0)
    {
        _FreeExtraSpace(OutputBuffer, item, size, freeSize);
    }
#endif
}
#endif

static void
_AppendString(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctINT Indent,
    IN gctCONST_STRING Message,
    IN gctUINT ArgumentSize,
    IN gctPOINTER Data
    )
{
    gctUINT8_PTR messageData;
    gcsBUFITEM_STRING_PTR item;
    gctINT allocSize;

#if gcdALIGNBYSIZE
    gctUINT alignment;
    gctINT size, freeSize;
#endif

    /* Determine the maximum item size. */
    allocSize
        = gcmSIZEOF(gcsBUFITEM_STRING)
        + ArgumentSize
        + gcdVARARG_ALIGNMENT;

    /* Allocate prefix item. */
    item = (gcsBUFITEM_STRING_PTR) _AllocateItem(OutputBuffer, allocSize);

    /* Compute the initial message data pointer. */
    messageData = (gctUINT8_PTR) (item + 1);

    /* Align the data pointer as necessary. */
#if gcdALIGNBYSIZE
    alignment = gcmPTRALIGNMENT(messageData, gcdVARARG_ALIGNMENT);
    messageData += alignment;
#endif

    /* Set item data. */
    item->type            = gcvBUFITEM_STRING;
    item->indent          = Indent;
    item->message         = Message;
    item->messageData     = messageData;
    item->messageDataSize = ArgumentSize;

    /* Copy argument value. */
    if (ArgumentSize != 0)
    {
        gcmkMEMCPY(messageData, Data, ArgumentSize);
    }

#if gcdALIGNBYSIZE
    /* Compute the actual node size. */
    size = gcmSIZEOF(gcsBUFITEM_STRING) + ArgumentSize + alignment;

    /* Free extra memory if any. */
    freeSize = allocSize - size;
    if (freeSize != 0)
    {
        _FreeExtraSpace(OutputBuffer, item, size, freeSize);
    }
#endif
}

static void
_AppendCopy(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctINT Indent,
    IN gctCONST_STRING Message,
    IN gctUINT ArgumentSize,
    IN gctPOINTER Data
    )
{
    gctUINT8_PTR messageData;
    gcsBUFITEM_COPY_PTR item;
    gctINT allocSize;
    gctINT messageLength;
    gctCONST_STRING message;

#if gcdALIGNBYSIZE
    gctUINT alignment;
    gctINT size, freeSize;
#endif

    /* Get the length of the string. */
    messageLength = strlen(Message) + 1;

    /* Determine the maximum item size. */
    allocSize
        = gcmSIZEOF(gcsBUFITEM_COPY)
        + messageLength
        + ArgumentSize
        + gcdVARARG_ALIGNMENT;

    /* Allocate prefix item. */
    item = (gcsBUFITEM_COPY_PTR) _AllocateItem(OutputBuffer, allocSize);

    /* Determine the message placement. */
    message = (gctCONST_STRING) (item + 1);

    /* Compute the initial message data pointer. */
    messageData = (gctUINT8_PTR) message + messageLength;

    /* Align the data pointer as necessary. */
#if gcdALIGNBYSIZE
    if (ArgumentSize == 0)
    {
        alignment = 0;
    }
    else
    {
        alignment = gcmPTRALIGNMENT(messageData, gcdVARARG_ALIGNMENT);
        messageData += alignment;
    }
#endif

    /* Set item data. */
    item->type            = gcvBUFITEM_COPY;
    item->indent          = Indent;
    item->messageData     = messageData;
    item->messageDataSize = ArgumentSize;

    /* Copy the message. */
    gcmkMEMCPY((gctPOINTER) message, Message, messageLength);

    /* Copy argument value. */
    if (ArgumentSize != 0)
    {
        gcmkMEMCPY(messageData, Data, ArgumentSize);
    }

#if gcdALIGNBYSIZE
    /* Compute the actual node size. */
    size
        = gcmSIZEOF(gcsBUFITEM_COPY)
        + messageLength
        + ArgumentSize
        + alignment;

    /* Free extra memory if any. */
    freeSize = allocSize - size;
    if (freeSize != 0)
    {
        _FreeExtraSpace(OutputBuffer, item, size, freeSize);
    }
#endif
}

static void
_AppendBuffer(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctINT Indent,
    IN gctPOINTER PrefixData,
    IN gctPOINTER Data,
    IN gctUINT Address,
    IN gctUINT DataSize,
    IN gceDUMP_BUFFER Type,
    IN gctUINT32 DmaAddress
    )
{
#if gcdHAVEPREFIX
    gctUINT8_PTR prefixData;
    gcsBUFITEM_BUFFER_PTR item;
    gctINT allocSize;
    gctPOINTER data;

#if gcdALIGNBYSIZE
    gctUINT alignment;
    gctINT size, freeSize;
#endif

    gcmDBGASSERT(DataSize != 0, "%d", DataSize);
    gcmDBGASSERT(Data != gcvNULL, "%p", Data);

    /* Determine the maximum item size. */
    allocSize
        = gcmSIZEOF(gcsBUFITEM_BUFFER)
        + gcdPREFIX_SIZE
        + gcdPREFIX_ALIGNMENT
        + DataSize;

    /* Allocate prefix item. */
    item = (gcsBUFITEM_BUFFER_PTR) _AllocateItem(OutputBuffer, allocSize);

    /* Compute the initial prefix data pointer. */
    prefixData = (gctUINT8_PTR) (item + 1);

#if gcdALIGNBYSIZE
    /* Align the data pointer as necessary. */
    alignment = gcmPTRALIGNMENT(prefixData, gcdPREFIX_ALIGNMENT);
    prefixData += alignment;
#endif

    /* Set item data. */
    item->type       = gcvBUFITEM_BUFFER;
    item->indent     = Indent;
    item->bufferType = Type;
    item->dataSize   = DataSize;
    item->address    = Address;
    item->prefixData = prefixData;

#if gcdDMA_BUFFER_COUNT && (gcdTHREAD_BUFFERS == 1)
    item->dmaAddress = DmaAddress;
#endif

    /* Copy prefix data. */
    gcmkMEMCPY(prefixData, PrefixData, gcdPREFIX_SIZE);

    /* Compute the data pointer. */
    data = prefixData + gcdPREFIX_SIZE;

    /* Copy argument value. */
    gcmkMEMCPY(data, Data, DataSize);

#if gcdALIGNBYSIZE
    /* Compute the actual node size. */
    size
        = gcmSIZEOF(gcsBUFITEM_BUFFER)
        + gcdPREFIX_SIZE
        + alignment
        + DataSize;

    /* Free extra memory if any. */
    freeSize = allocSize - size;
    if (freeSize != 0)
    {
        _FreeExtraSpace(OutputBuffer, item, size, freeSize);
    }
#endif
#else
    gcsBUFITEM_BUFFER_PTR item;
    gctINT size;

    gcmDBGASSERT(DataSize != 0, "%d", DataSize);
    gcmDBGASSERT(Data != gcvNULL, "%p", Data);

    /* Determine the maximum item size. */
    size = gcmSIZEOF(gcsBUFITEM_BUFFER) + DataSize;

    /* Allocate prefix item. */
    item = (gcsBUFITEM_BUFFER_PTR) _AllocateItem(OutputBuffer, size);

    /* Set item data. */
    item->type     = gcvBUFITEM_BUFFER;
    item->indent   = Indent;
    item->dataSize = DataSize;
    item->address  = Address;

    /* Copy argument value. */
    gcmkMEMCPY(item + 1, Data, DataSize);
#endif
}
#endif

static gcmINLINE void
_InitBuffers(
    void
    )
{
    int i;

    if (_outputBufferHead == gcvNULL)
    {
        for (i = 0; i < gcdTHREAD_BUFFERS; i += 1)
        {
            if (_outputBufferTail == gcvNULL)
            {
                _outputBufferHead = &_outputBuffer[i];
            }
            else
            {
                _outputBufferTail->next = &_outputBuffer[i];
            }

#if gcdTHREAD_BUFFERS > 1
            _outputBuffer[i].threadID = ~0U;
#endif

            _outputBuffer[i].prev = _outputBufferTail;
            _outputBuffer[i].next =  gcvNULL;

            _outputBufferTail = &_outputBuffer[i];
        }
    }
}

static gcmINLINE gcsBUFFERED_OUTPUT_PTR
_GetOutputBuffer(
    void
    )
{
    gcsBUFFERED_OUTPUT_PTR outputBuffer;

#if gcdTHREAD_BUFFERS > 1
    /* Get the current thread ID. */
    gctUINT32 ThreadID = gcmkGETTHREADID();

    /* Locate the output buffer for the thread. */
    outputBuffer = _outputBufferHead;

    while (outputBuffer != gcvNULL)
    {
        if (outputBuffer->threadID == ThreadID)
        {
            break;
        }

        outputBuffer = outputBuffer->next;
    }

    /* No matching buffer found? */
    if (outputBuffer == gcvNULL)
    {
        /* Get the tail for the buffer. */
        outputBuffer = _outputBufferTail;

        /* Move it to the head. */
        _outputBufferTail       = _outputBufferTail->prev;
        _outputBufferTail->next = gcvNULL;

        outputBuffer->prev = gcvNULL;
        outputBuffer->next = _outputBufferHead;

        _outputBufferHead->prev = outputBuffer;
        _outputBufferHead       = outputBuffer;

        /* Reset the buffer. */
        outputBuffer->threadID   = ThreadID;
#if gcdBUFFERED_OUTPUT
        outputBuffer->start      = 0;
        outputBuffer->index      = 0;
        outputBuffer->count      = 0;
#endif
#if gcdSHOW_LINE_NUMBER
        outputBuffer->lineNumber = 0;
#endif
    }
#else
    outputBuffer = _outputBufferHead;
#endif

    return outputBuffer;
}

static gcmINLINE int _GetArgumentSize(
    IN gctCONST_STRING Message
    )
{
    int i, count;

    gcmDBGASSERT(Message != gcvNULL, "%p", Message);

    for (i = 0, count = 0; Message[i]; i += 1)
    {
        if (Message[i] == '%')
        {
            count += 1;
        }
    }

    return count * gcmSIZEOF(gctUINT32);
}

#if gcdHAVEPREFIX
static void
_InitPrefixData(
    IN gcsBUFFERED_OUTPUT_PTR OutputBuffer,
    IN gctPOINTER Data
    )
{
    gctUINT8_PTR data  = (gctUINT8_PTR) Data;

#if gcdSHOW_TIME
    {
        gctUINT64 time;
        gckOS_GetProfileTick(&time);
        gcmkALIGNPTR(gctUINT8_PTR, data, gcmSIZEOF(gctUINT64));
        * ((gctUINT64_PTR) data) = time;
        data += gcmSIZEOF(gctUINT64);
    }
#endif

#if gcdSHOW_LINE_NUMBER
    {
        gcmkALIGNPTR(gctUINT8_PTR, data, gcmSIZEOF(gctUINT64));
        * ((gctUINT64_PTR) data) = OutputBuffer->lineNumber;
        data += gcmSIZEOF(gctUINT64);
    }
#endif

#if gcdSHOW_PROCESS_ID
    {
        gcmkALIGNPTR(gctUINT8_PTR, data, gcmSIZEOF(gctUINT32));
        * ((gctUINT32_PTR) data) = gcmkGETPROCESSID();
        data += gcmSIZEOF(gctUINT32);
    }
#endif

#if gcdSHOW_THREAD_ID
    {
        gcmkALIGNPTR(gctUINT8_PTR, data, gcmSIZEOF(gctUINT32));
        * ((gctUINT32_PTR) data) = gcmkGETTHREADID();
    }
#endif
}
#endif

static void
_Print(
    IN gctUINT ArgumentSize,
    IN gctBOOL CopyMessage,
    IN gctCONST_STRING Message,
    IN gctARGUMENTS * Arguments
    )
{
    gcsBUFFERED_OUTPUT_PTR outputBuffer;
    gcmkDECLARE_LOCK(lockHandle);

    gcmkLOCKSECTION(lockHandle);

    /* Initialize output buffer list. */
    _InitBuffers();

    /* Locate the proper output buffer. */
    outputBuffer = _GetOutputBuffer();

    /* Update the line number. */
#if gcdSHOW_LINE_NUMBER
    outputBuffer->lineNumber += 1;
#endif

    /* Print prefix. */
#if gcdHAVEPREFIX
    {
        gctUINT8_PTR alignedPrefixData;
        gctUINT8 prefixData[gcdPREFIX_SIZE + gcdPREFIX_ALIGNMENT];

        /* Compute aligned pointer. */
        alignedPrefixData = prefixData;
        gcmkALIGNPTR(gctUINT8_PTR, alignedPrefixData, gcdPREFIX_ALIGNMENT);

        /* Initialize the prefix data. */
        _InitPrefixData(outputBuffer, alignedPrefixData);

        /* Print the prefix. */
        gcdOUTPUTPREFIX(outputBuffer, alignedPrefixData);
    }
#endif

    /* Form the indent string. */
    if (strncmp(Message, "--", 2) == 0)
    {
        outputBuffer->indent -= 2;
    }

    /* Print the message. */
    if (CopyMessage)
    {
        gcdOUTPUTCOPY(
            outputBuffer, outputBuffer->indent,
            Message, ArgumentSize, (gctPOINTER) Arguments
            );
    }
    else
    {
        gcdOUTPUTSTRING(
            outputBuffer, outputBuffer->indent,
            Message, ArgumentSize, ((gctPOINTER) Arguments)
            );
    }

    /* Check increasing indent. */
    if (strncmp(Message, "++", 2) == 0)
    {
        outputBuffer->indent += 2;
    }

    gcmkUNLOCKSECTION(lockHandle);
}


/******************************************************************************\
********************************* Debug Macros *********************************
\******************************************************************************/

#ifdef __QNXNTO__

extern volatile unsigned g_nQnxInIsrs;

#define gcmDEBUGPRINT(ArgumentSize, CopyMessage, Message) \
{ \
    if (atomic_add_value(&g_nQnxInIsrs, 1) == 0) \
    { \
        gctARGUMENTS __arguments__; \
        gcmkARGUMENTS_START(__arguments__, Message); \
        _Print(ArgumentSize, CopyMessage, Message, &__arguments__); \
        gcmkARGUMENTS_END(__arguments__); \
    } \
    atomic_sub(&g_nQnxInIsrs, 1); \
}

#else

#define gcmDEBUGPRINT(ArgumentSize, CopyMessage, Message) \
{ \
    gctARGUMENTS __arguments__; \
    gcmkARGUMENTS_START(__arguments__, Message); \
    _Print(ArgumentSize, CopyMessage, Message, &__arguments__); \
    gcmkARGUMENTS_END(__arguments__); \
}

#endif

/******************************************************************************\
********************************** Debug Code **********************************
\******************************************************************************/

/*******************************************************************************
**
**  gckOS_Print
**
**  Send a message to the debugger.
**
**  INPUT:
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_Print(
    IN gctCONST_STRING Message,
    ...
    )
{
    gcmDEBUGPRINT(_GetArgumentSize(Message), gcvFALSE, Message);
}

/*******************************************************************************
**
**  gckOS_PrintN
**
**  Send a message to the debugger.
**
**  INPUT:
**
**      gctUINT ArgumentSize
**          The size of the optional arguments in bytes.
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_PrintN(
    IN gctUINT ArgumentSize,
    IN gctCONST_STRING Message,
    ...
    )
{
    gcmDEBUGPRINT(ArgumentSize, gcvFALSE, Message);
}

/*******************************************************************************
**
**  gckOS_CopyPrint
**
**  Send a message to the debugger. If in buffered output mode, the entire
**  message will be copied into the buffer instead of using the pointer to
**  the string.
**
**  INPUT:
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_CopyPrint(
    IN gctCONST_STRING Message,
    ...
    )
{
    gcmDEBUGPRINT(_GetArgumentSize(Message), gcvTRUE, Message);
}

/*******************************************************************************
**
**  gckOS_DumpBuffer
**
**  Print the contents of the specified buffer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctPOINTER Buffer
**          Pointer to the buffer to print.
**
**      gctUINT Size
**          Size of the buffer.
**
**      gceDUMP_BUFFER Type
**          Buffer type.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_DumpBuffer(
    IN gckOS Os,
    IN gctPOINTER Buffer,
    IN gctUINT Size,
    IN gceDUMP_BUFFER Type,
    IN gctBOOL CopyMessage
    )
{
    gctPHYS_ADDR_T physical;
    gctUINT32 address                   = 0;
    gcsBUFFERED_OUTPUT_PTR outputBuffer = gcvNULL;
    static gctBOOL userLocked;
    gctCHAR *buffer                     = (gctCHAR*)Buffer;

    gcmkDECLARE_LOCK(lockHandle);

    /* Request lock when not coming from user,
       or coming from user and not yet locked
          and message is starting with @[. */
    if (Type == gceDUMP_BUFFER_FROM_USER)
    {
        if ((Size > 2)
        && (buffer[0] == '@')
        && (buffer[1] == '['))
        {
            /* Beginning of a user dump. */
            gcmkLOCKSECTION(lockHandle);
            userLocked = gcvTRUE;
        }
        /* Else, let it pass through. */
    }
    else
    {
        gcmkLOCKSECTION(lockHandle);
        userLocked = gcvFALSE;
    }

    if (Buffer != gcvNULL)
    {
        /* Initialize output buffer list. */
        _InitBuffers();

        /* Locate the proper output buffer. */
        outputBuffer = _GetOutputBuffer();

        /* Update the line number. */
#if gcdSHOW_LINE_NUMBER
        outputBuffer->lineNumber += 1;
#endif

        /* Get the physical address of the buffer. */
        if (Type != gceDUMP_BUFFER_FROM_USER)
        {
            gcmkVERIFY_OK(gckOS_GetPhysicalAddress(Os, Buffer, &physical));
            gcmkSAFECASTPHYSADDRT(address, physical);
        }
        else
        {
            address = 0;
        }

#if gcdHAVEPREFIX
        {
            gctUINT8_PTR alignedPrefixData;
            gctUINT8 prefixData[gcdPREFIX_SIZE + gcdPREFIX_ALIGNMENT];

            /* Compute aligned pointer. */
            alignedPrefixData = prefixData;
            gcmkALIGNPTR(gctUINT8_PTR, alignedPrefixData, gcdPREFIX_ALIGNMENT);

            /* Initialize the prefix data. */
            _InitPrefixData(outputBuffer, alignedPrefixData);

            /* Print/schedule the buffer. */
            gcdOUTPUTBUFFER(
                outputBuffer, outputBuffer->indent,
                alignedPrefixData, Buffer, address, Size, Type, 0
                );
        }
#else
        /* Print/schedule the buffer. */
        if (Type == gceDUMP_BUFFER_FROM_USER)
        {
            gcdOUTPUTSTRING(
                outputBuffer, outputBuffer->indent,
                Buffer, 0, gcvNULL
                );
        }
        else
        {
            gcdOUTPUTBUFFER(
                outputBuffer, outputBuffer->indent,
                gcvNULL, Buffer, address, Size, Type, 0
                );
        }
#endif
    }

    /* Unlock when not coming from user,
       or coming from user and not yet locked. */
    if (userLocked)
    {
        if ((Size > 4)
        && (buffer[0] == ']')
        && (buffer[1] == ' ')
        && (buffer[2] == '-')
        && (buffer[3] == '-'))
        {
            /* End of a user dump. */
            gcmkUNLOCKSECTION(lockHandle);
            userLocked = gcvFALSE;
        }
        /* Else, let it pass through, don't unlock. */
    }
    else
    {
        gcmkUNLOCKSECTION(lockHandle);
    }
}

/*******************************************************************************
**
**  gckOS_DebugTrace
**
**  Send a leveled message to the debugger.
**
**  INPUT:
**
**      gctUINT32 Level
**          Debug level of message.
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_DebugTrace(
    IN gctUINT32 Level,
    IN gctCONST_STRING Message,
    ...
    )
{
    if (Level > _debugLevel)
    {
        return;
    }

    gcmDEBUGPRINT(_GetArgumentSize(Message), gcvFALSE, Message);
}

/*******************************************************************************
**
**  gckOS_DebugTraceN
**
**  Send a leveled message to the debugger.
**
**  INPUT:
**
**      gctUINT32 Level
**          Debug level of message.
**
**      gctUINT ArgumentSize
**          The size of the optional arguments in bytes.
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_DebugTraceN(
    IN gctUINT32 Level,
    IN gctUINT ArgumentSize,
    IN gctCONST_STRING Message,
    ...
    )
{
    if (Level > _debugLevel)
    {
        return;
    }

    gcmDEBUGPRINT(ArgumentSize, gcvFALSE, Message);
}

/*******************************************************************************
**
**  gckOS_DebugTraceZone
**
**  Send a leveled and zoned message to the debugger.
**
**  INPUT:
**
**      gctUINT32 Level
**          Debug level for message.
**
**      gctUINT32 Zone
**          Debug zone for message.
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_DebugTraceZone(
    IN gctUINT32 Level,
    IN gctUINT32 Zone,
    IN gctCONST_STRING Message,
    ...
    )
{
    if ((Level > _debugLevel) || !(Zone & _debugZones))
    {
        return;
    }

    gcmDEBUGPRINT(_GetArgumentSize(Message), gcvFALSE, Message);
}

/*******************************************************************************
**
**  gckOS_DebugTraceZoneN
**
**  Send a leveled and zoned message to the debugger.
**
**  INPUT:
**
**      gctUINT32 Level
**          Debug level for message.
**
**      gctUINT32 Zone
**          Debug zone for message.
**
**      gctUINT ArgumentSize
**          The size of the optional arguments in bytes.
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_DebugTraceZoneN(
    IN gctUINT32 Level,
    IN gctUINT32 Zone,
    IN gctUINT ArgumentSize,
    IN gctCONST_STRING Message,
    ...
    )
{
    if ((Level > _debugLevel) || !(Zone & _debugZones))
    {
        return;
    }

    gcmDEBUGPRINT(ArgumentSize, gcvFALSE, Message);
}

/*******************************************************************************
**
**  gckOS_DebugBreak
**
**  Break into the debugger.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      Nothing.
*/
void
gckOS_DebugBreak(
    void
    )
{
    gckOS_DebugTrace(gcvLEVEL_ERROR, "%s(%d)", __FUNCTION__, __LINE__);
}

/*******************************************************************************
**
**  gckOS_DebugFatal
**
**  Send a message to the debugger and break into the debugger.
**
**  INPUT:
**
**      gctCONST_STRING Message
**          Pointer to message.
**
**      ...
**          Optional arguments.
**
**  OUTPUT:
**
**      Nothing.
*/
void
gckOS_DebugFatal(
    IN gctCONST_STRING Message,
    ...
    )
{
    gcmkPRINT_VERSION();
    gcmDEBUGPRINT(_GetArgumentSize(Message), gcvFALSE, Message);

    /* Break into the debugger. */
    gckOS_DebugBreak();
}

/*******************************************************************************
**
**  gckOS_SetDebugLevel
**
**  Set the debug level.
**
**  INPUT:
**
**      gctUINT32 Level
**          New debug level.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_SetDebugLevel(
    IN gctUINT32 Level
    )
{
    _debugLevel = Level;
}

/*******************************************************************************
**
**  gckOS_SetDebugZone
**
**  Set the debug zone.
**
**  INPUT:
**
**      gctUINT32 Zone
**          New debug zone.
**
**  OUTPUT:
**
**      Nothing.
*/
void
gckOS_SetDebugZone(
    IN gctUINT32 Zone
    )
{
    _debugZones = Zone;
}

/*******************************************************************************
**
**  gckOS_SetDebugLevelZone
**
**  Set the debug level and zone.
**
**  INPUT:
**
**      gctUINT32 Level
**          New debug level.
**
**      gctUINT32 Zone
**          New debug zone.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_SetDebugLevelZone(
    IN gctUINT32 Level,
    IN gctUINT32 Zone
    )
{
    _debugLevel = Level;
    _debugZones = Zone;
}

/*******************************************************************************
**
**  gckOS_SetDebugZones
**
**  Enable or disable debug zones.
**
**  INPUT:
**
**      gctUINT32 Zones
**          Debug zones to enable or disable.
**
**      gctBOOL Enable
**          Set to gcvTRUE to enable the zones (or the Zones with the current
**          zones) or gcvFALSE to disable the specified Zones.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_SetDebugZones(
    IN gctUINT32 Zones,
    IN gctBOOL Enable
    )
{
    if (Enable)
    {
        /* Enable the zones. */
        _debugZones |= Zones;
    }
    else
    {
        /* Disable the zones. */
        _debugZones &= ~Zones;
    }
}

/*******************************************************************************
**
**  gckOS_Verify
**
**  Called to verify the result of a function call.
**
**  INPUT:
**
**      gceSTATUS Status
**          Function call result.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_Verify(
    IN gceSTATUS status
    )
{
    _lastError = status;
}

/*******************************************************************************
**
**  gckOS_DebugFlush
**
**  Force messages to be flushed out.
**
**  INPUT:
**
**      gctCONST_STRING CallerName
**          Name of the caller function.
**
**      gctUINT LineNumber
**          Line number of the caller.
**
**      gctUINT32 DmaAddress
**          The current DMA address or ~0U to ignore.
**
**  OUTPUT:
**
**      Nothing.
*/

void
gckOS_DebugFlush(
    gctCONST_STRING CallerName,
    gctUINT LineNumber,
    gctUINT32 DmaAddress
    )
{
#if gcdBUFFERED_OUTPUT
    _DirectPrint("\nFlush requested by %s(%d).\n\n", CallerName, LineNumber);
    _Flush(DmaAddress);
#endif
}
gctCONST_STRING
gckOS_DebugStatus2Name(
    gceSTATUS status
    )
{
    switch (status)
    {
    case gcvSTATUS_OK:
        return "gcvSTATUS_OK";
    case gcvSTATUS_TRUE:
        return "gcvSTATUS_TRUE";
    case gcvSTATUS_NO_MORE_DATA:
        return "gcvSTATUS_NO_MORE_DATA";
    case gcvSTATUS_CACHED:
        return "gcvSTATUS_CACHED";
    case gcvSTATUS_MIPMAP_TOO_LARGE:
        return "gcvSTATUS_MIPMAP_TOO_LARGE";
    case gcvSTATUS_NAME_NOT_FOUND:
        return "gcvSTATUS_NAME_NOT_FOUND";
    case gcvSTATUS_NOT_OUR_INTERRUPT:
        return "gcvSTATUS_NOT_OUR_INTERRUPT";
    case gcvSTATUS_MISMATCH:
        return "gcvSTATUS_MISMATCH";
    case gcvSTATUS_MIPMAP_TOO_SMALL:
        return "gcvSTATUS_MIPMAP_TOO_SMALL";
    case gcvSTATUS_LARGER:
        return "gcvSTATUS_LARGER";
    case gcvSTATUS_SMALLER:
        return "gcvSTATUS_SMALLER";
    case gcvSTATUS_CHIP_NOT_READY:
        return "gcvSTATUS_CHIP_NOT_READY";
    case gcvSTATUS_NEED_CONVERSION:
        return "gcvSTATUS_NEED_CONVERSION";
    case gcvSTATUS_SKIP:
        return "gcvSTATUS_SKIP";
    case gcvSTATUS_DATA_TOO_LARGE:
        return "gcvSTATUS_DATA_TOO_LARGE";
    case gcvSTATUS_INVALID_CONFIG:
        return "gcvSTATUS_INVALID_CONFIG";
    case gcvSTATUS_CHANGED:
        return "gcvSTATUS_CHANGED";
    case gcvSTATUS_NOT_SUPPORT_DITHER:
        return "gcvSTATUS_NOT_SUPPORT_DITHER";

    case gcvSTATUS_INVALID_ARGUMENT:
        return "gcvSTATUS_INVALID_ARGUMENT";
    case gcvSTATUS_INVALID_OBJECT:
        return "gcvSTATUS_INVALID_OBJECT";
    case gcvSTATUS_OUT_OF_MEMORY:
        return "gcvSTATUS_OUT_OF_MEMORY";
    case gcvSTATUS_MEMORY_LOCKED:
        return "gcvSTATUS_MEMORY_LOCKED";
    case gcvSTATUS_MEMORY_UNLOCKED:
        return "gcvSTATUS_MEMORY_UNLOCKED";
    case gcvSTATUS_HEAP_CORRUPTED:
        return "gcvSTATUS_HEAP_CORRUPTED";
    case gcvSTATUS_GENERIC_IO:
        return "gcvSTATUS_GENERIC_IO";
    case gcvSTATUS_INVALID_ADDRESS:
        return "gcvSTATUS_INVALID_ADDRESS";
    case gcvSTATUS_CONTEXT_LOSSED:
        return "gcvSTATUS_CONTEXT_LOSSED";
    case gcvSTATUS_TOO_COMPLEX:
        return "gcvSTATUS_TOO_COMPLEX";
    case gcvSTATUS_BUFFER_TOO_SMALL:
        return "gcvSTATUS_BUFFER_TOO_SMALL";
    case gcvSTATUS_INTERFACE_ERROR:
        return "gcvSTATUS_INTERFACE_ERROR";
    case gcvSTATUS_NOT_SUPPORTED:
        return "gcvSTATUS_NOT_SUPPORTED";
    case gcvSTATUS_MORE_DATA:
        return "gcvSTATUS_MORE_DATA";
    case gcvSTATUS_TIMEOUT:
        return "gcvSTATUS_TIMEOUT";
    case gcvSTATUS_OUT_OF_RESOURCES:
        return "gcvSTATUS_OUT_OF_RESOURCES";
    case gcvSTATUS_INVALID_DATA:
        return "gcvSTATUS_INVALID_DATA";
    case gcvSTATUS_INVALID_MIPMAP:
        return "gcvSTATUS_INVALID_MIPMAP";
    case gcvSTATUS_NOT_FOUND:
        return "gcvSTATUS_NOT_FOUND";
    case gcvSTATUS_NOT_ALIGNED:
        return "gcvSTATUS_NOT_ALIGNED";
    case gcvSTATUS_INVALID_REQUEST:
        return "gcvSTATUS_INVALID_REQUEST";
    case gcvSTATUS_GPU_NOT_RESPONDING:
        return "gcvSTATUS_GPU_NOT_RESPONDING";
    case gcvSTATUS_TIMER_OVERFLOW:
        return "gcvSTATUS_TIMER_OVERFLOW";
    case gcvSTATUS_VERSION_MISMATCH:
        return "gcvSTATUS_VERSION_MISMATCH";
    case gcvSTATUS_LOCKED:
        return "gcvSTATUS_LOCKED";
    case gcvSTATUS_INTERRUPTED:
        return "gcvSTATUS_INTERRUPTED";
    case gcvSTATUS_DEVICE:
        return "gcvSTATUS_DEVICE";
    case gcvSTATUS_NOT_MULTI_PIPE_ALIGNED:
        return "gcvSTATUS_NOT_MULTI_PIPE_ALIGNED";

    /* Linker errors. */
    case gcvSTATUS_GLOBAL_TYPE_MISMATCH:
        return "gcvSTATUS_GLOBAL_TYPE_MISMATCH";
    case gcvSTATUS_TOO_MANY_ATTRIBUTES:
        return "gcvSTATUS_TOO_MANY_ATTRIBUTES";
    case gcvSTATUS_TOO_MANY_UNIFORMS:
        return "gcvSTATUS_TOO_MANY_UNIFORMS";
    case gcvSTATUS_TOO_MANY_SAMPLER:
        return "gcvSTATUS_TOO_MANY_SAMPLER";
    case gcvSTATUS_TOO_MANY_VARYINGS:
        return "gcvSTATUS_TOO_MANY_VARYINGS";
    case gcvSTATUS_UNDECLARED_VARYING:
        return "gcvSTATUS_UNDECLARED_VARYING";
    case gcvSTATUS_VARYING_TYPE_MISMATCH:
        return "gcvSTATUS_VARYING_TYPE_MISMATCH";
    case gcvSTATUS_MISSING_MAIN:
        return "gcvSTATUS_MISSING_MAIN";
    case gcvSTATUS_NAME_MISMATCH:
        return "gcvSTATUS_NAME_MISMATCH";
    case gcvSTATUS_INVALID_INDEX:
        return "gcvSTATUS_INVALID_INDEX";
    case gcvSTATUS_UNIFORM_MISMATCH:
        return "gcvSTATUS_UNIFORM_MISMATCH";
    case gcvSTATUS_UNSAT_LIB_SYMBOL:
        return "gcvSTATUS_UNSAT_LIB_SYMBOL";
    case gcvSTATUS_TOO_MANY_SHADERS:
        return "gcvSTATUS_TOO_MANY_SHADERS";
    case gcvSTATUS_LINK_INVALID_SHADERS:
        return "gcvSTATUS_LINK_INVALID_SHADERS";
    case gcvSTATUS_CS_NO_WORKGROUP_SIZE:
        return "gcvSTATUS_CS_NO_WORKGROUP_SIZE";
    case gcvSTATUS_LINK_LIB_ERROR:
        return "gcvSTATUS_LINK_LIB_ERROR";
    case gcvSTATUS_SHADER_VERSION_MISMATCH:
        return "gcvSTATUS_SHADER_VERSION_MISMATCH";
    case gcvSTATUS_TOO_MANY_INSTRUCTION:
        return "gcvSTATUS_TOO_MANY_INSTRUCTION";
    case gcvSTATUS_SSBO_MISMATCH:
        return "gcvSTATUS_SSBO_MISMATCH";
    case gcvSTATUS_TOO_MANY_OUTPUT:
        return "gcvSTATUS_TOO_MANY_OUTPUT";
    case gcvSTATUS_TOO_MANY_INPUT:
        return "gcvSTATUS_TOO_MANY_INPUT";
    case gcvSTATUS_NOT_SUPPORT_CL:
        return "gcvSTATUS_NOT_SUPPORT_CL";
    case gcvSTATUS_NOT_SUPPORT_INTEGER:
        return "gcvSTATUS_NOT_SUPPORT_INTEGER";

    /* Compiler errors. */
    case gcvSTATUS_COMPILER_FE_PREPROCESSOR_ERROR:
        return "gcvSTATUS_COMPILER_FE_PREPROCESSOR_ERROR";
    case gcvSTATUS_COMPILER_FE_PARSER_ERROR:
        return "gcvSTATUS_COMPILER_FE_PARSER_ERROR";

    default:
        return "nil";
    }
}

/*******************************************************************************
***** Binary Trace *************************************************************
*******************************************************************************/

/*******************************************************************************
**  _VerifyMessage
**
**  Verify a binary trace message, decode it to human readable string and print
**  it.
**
**  ARGUMENTS:
**
**      gctCONST_STRING Buffer
**          Pointer to buffer to store.
**
**      gctSIZE_T Bytes
**          Buffer length.
*/
void
_VerifyMessage(
    IN gctCONST_STRING Buffer,
    IN gctSIZE_T Bytes
    )
{
    char arguments[150] = {0};
    char format[100] = {0};

    gctSTRING function;
    gctPOINTER args;
    gctUINT32 numArguments;
    int i = 0;
    gctUINT32 functionBytes;

    gcsBINARY_TRACE_MESSAGE_PTR message = (gcsBINARY_TRACE_MESSAGE_PTR)Buffer;

    /* Check signature. */
    if (message->signature != 0x7FFFFFFF)
    {
        gcmkPRINT("Signature error");
        return;
    }

    /* Get function name. */
    function = (gctSTRING)&message->payload;
    functionBytes = (gctUINT32)strlen(function) + 1;

    /* Get arguments number. */
    numArguments = message->numArguments;

    /* Get arguments . */
    args = function + functionBytes;

    /* Prepare format string. */
    while (numArguments--)
    {
        format[i++] = '%';
        format[i++] = 'x';
        format[i++] = ' ';
    }

    format[i] = '\0';

    if (numArguments)
    {
        gcmkVSPRINTF(arguments, 150, format, (gctARGUMENTS *) &args);
    }

    gcmkPRINT("[%d](%d): %s(%d) %s",
             message->pid,
             message->tid,
             function,
             message->line,
             arguments);
}


/*******************************************************************************
**  gckOS_WriteToRingBuffer
**
**  Store a buffer to ring buffer.
**
**  ARGUMENTS:
**
**      gctCONST_STRING Buffer
**          Pointer to buffer to store.
**
**      gctSIZE_T Bytes
**          Buffer length.
*/
void
gckOS_WriteToRingBuffer(
    IN gctCONST_STRING Buffer,
    IN gctSIZE_T Bytes
    )
{

}

/*******************************************************************************
**  gckOS_BinaryTrace
**
**  Output a binary trace message.
**
**  ARGUMENTS:
**
**      gctCONST_STRING Function
**          Pointer to function name.
**
**      gctINT Line
**          Line number.
**
**      gctCONST_STRING Text OPTIONAL
**          Optional pointer to a descriptive text.
**
**      ...
**          Optional arguments to the descriptive text.
*/
void
gckOS_BinaryTrace(
    IN gctCONST_STRING Function,
    IN gctINT Line,
    IN gctCONST_STRING Text OPTIONAL,
    ...
    )
{
    static gctUINT32 messageSignature = 0x7FFFFFFF;
    char buffer[gcdBINARY_TRACE_MESSAGE_SIZE];
    gctUINT32 numArguments = 0;
    gctUINT32 functionBytes;
    gctUINT32 i = 0;
    gctSTRING payload;
    gcsBINARY_TRACE_MESSAGE_PTR message = (gcsBINARY_TRACE_MESSAGE_PTR)buffer;

    /* Calculate arguments number. */
    if (Text)
    {
        while (Text[i] != '\0')
        {
            if (Text[i] == '%')
            {
                numArguments++;
            }
            i++;
        }
    }

    message->signature    = messageSignature;
    message->pid          = gcmkGETPROCESSID();
    message->tid          = gcmkGETTHREADID();
    message->line         = Line;
    message->numArguments = numArguments;

    payload = (gctSTRING)&message->payload;

    /* Function name. */
    functionBytes = (gctUINT32)gcmkSTRLEN(Function) + 1;
    gcmkMEMCPY(payload, Function, functionBytes);

    /* Advance to next payload. */
    payload += functionBytes;

    /* Arguments value. */
    if (numArguments)
    {
        gctARGUMENTS p;
        gcmkARGUMENTS_START(p, Text);

        for (i = 0; i < numArguments; ++i)
        {
            gctPOINTER value = gcmkARGUMENTS_ARG(p, gctPOINTER);
            gcmkMEMCPY(payload, &value, gcmSIZEOF(gctPOINTER));
            payload += gcmSIZEOF(gctPOINTER);
        }

        gcmkARGUMENTS_END(p);
    }

    gcmkASSERT(payload - buffer <= gcdBINARY_TRACE_MESSAGE_SIZE);


    /* Send buffer to ring buffer. */
    gckOS_WriteToRingBuffer(buffer, (gctUINT32)(payload - buffer));
}

