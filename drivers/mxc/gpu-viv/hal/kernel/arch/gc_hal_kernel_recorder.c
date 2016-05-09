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


#include "gc_hal.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_context.h"

/*
 *                          -----------------------
 *                          HARDWARE STATE RECORDER
 *                          -----------------------
 *
 * State mirror buffer is used to 'mirror' hardware states since hardware
 * states can't be dumpped. It is a context buffer which stores 'global'
 * context.
 *
 * For each commit, state recorder
 * 1) Records context buffer (if there is) and command buffers in this commit.
 * 2) Parse those buffers to estimate the state changed.
 * 3) Stores result to a mirror buffer.
 *
 * == Commit 0 ====================================================================
 *
 *      Context Buffer 0
 *
 *      Command Buffer 0
 *
 *      Mirror Buffer  0  <- Context Buffer 0 + Command Buffer 0
 *
 * == Commit 1 ====================================================================
 *
 *      Command Buffer 1
 *
 *      Mirror Buffer  1  <- Command buffer 1 + Mirror Buffer 0
 *
 * == Commit 2 ====================================================================
 *
 *      Context Buffer 2 (optional)
 *
 *      Command Buffer 2
 *
 *      Mirror  Buffer 2  <- Command buffer 2 + Context Buffer 2 + Mirror Buffer 1
 *
 * == Commit N ====================================================================
 *
 * For Commit N, these buffers are needed to reproduce hardware's behavior in
 * this commit.
 *
 *  Mirror  Buffer [N - 1] : State Mirror accumlated by past commits,
 *                           which is used to restore hardware state.
 *  Context Buffer [N]     :
 *  Command Buffer [N]     : Command buffer executed by hardware in this commit.
 *
 *  If sequence of states programming matters, hardware's behavior can't be reproduced,
 *  but the state values stored in mirror buffer are assuring.
 */

/* Queue size. */
#define gcdNUM_RECORDS  6

typedef struct _gcsPARSER_HANDLER * gckPARSER_HANDLER;

typedef void
(*HandlerFunction)(
    IN gckPARSER_HANDLER Handler,
    IN gctUINT32 Addr,
    IN gctUINT32 Data
    );

typedef struct _gcsPARSER_HANDLER
{
    gctUINT32           type;
    gctUINT32           cmd;
    gctPOINTER          private;
    HandlerFunction     function;
}
gcsPARSER_HANDLER;

typedef struct _gcsPARSER * gckPARSER;
typedef struct _gcsPARSER
{
    gctUINT8_PTR        currentCmdBufferAddr;

    /* Current command. */
    gctUINT32           lo;
    gctUINT32           hi;

    gctUINT8            cmdOpcode;
    gctUINT16           cmdAddr;
    gctUINT32           cmdSize;
    gctUINT32           cmdRectCount;
    gctUINT8            skip;
    gctUINT32           skipCount;

    gctBOOL             allow;
    gctBOOL             stop;

    /* Callback used by parser to handle a command. */
    gckPARSER_HANDLER   commandHandler;
}
gcsPARSER;

typedef struct _gcsMIRROR
{
    gctUINT32_PTR       logical[gcdNUM_RECORDS];
    gctUINT32           bytes;
    gcsSTATE_MAP_PTR    map;
    gctSIZE_T           maxState;
}
gcsMIRROR;

typedef struct _gcsDELTA
{
    gctUINT64           commitStamp;
    gctUINT32_PTR       command;
    gctUINT32           commandBytes;
    gctUINT32_PTR       context;
    gctUINT32           contextBytes;
}
gcsDELTA;

typedef struct _gcsRECORDER
{
    gckOS               os;
    gcsMIRROR           mirror;
    gcsDELTA            deltas[gcdNUM_RECORDS];

    /* Index of current record. */
    gctUINT             index;

    /* Number of records. */
    gctUINT             num;

    /* Plugin used by gckPARSER. */
    gcsPARSER_HANDLER   recorderHandler;
    gckPARSER           parser;
}
gcsRECORDER;


/******************************************************************************\
***************************** Command Buffer Parser ****************************
\******************************************************************************/

/*
** Command buffer parser checks command buffer in FE's view to make sure there
** is no format error.
**
** Parser provide a callback mechnisam, so plug-in can be added to implement
** other functions.
*/

static void
_HandleLoadState(
    IN OUT gckPARSER Parser
    )
{
    gctUINT i;
    gctUINT32_PTR data = (gctUINT32_PTR)Parser->currentCmdBufferAddr;
    gctUINT32 cmdAddr = Parser->cmdAddr;

    if (Parser->commandHandler == gcvNULL
     || Parser->commandHandler->cmd != 0x01
    )
    {
        /* No handler for this command. */
        return;
    }

    for (i = 0; i < Parser->cmdSize; i++)
    {
        Parser->commandHandler->function(Parser->commandHandler, cmdAddr, *data);

        /* Advance to next state. */
        cmdAddr++;
        data++;
    }
}

static void
_GetCommand(
    IN OUT gckPARSER Parser
    )
{
    gctUINT32 * buffer = (gctUINT32 *)Parser->currentCmdBufferAddr;

    gctUINT16 cmdRectCount;
    gctUINT16 cmdDataCount;

    Parser->hi = buffer[0];
    Parser->lo = buffer[1];

    Parser->cmdOpcode = (((((gctUINT32) (Parser->hi)) >> (0 ? 31:27)) & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1)))))) );
    Parser->cmdRectCount = 1;

    switch (Parser->cmdOpcode)
    {
    case 0x01:
        /* Extract count. */
        Parser->cmdSize = (((((gctUINT32) (Parser->hi)) >> (0 ? 25:16)) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1)))))) );
        if (Parser->cmdSize == 0)
        {
            /* 0 means 1024. */
            Parser->cmdSize = 1024;
        }
        Parser->skip = (Parser->cmdSize & 0x1) ? 0 : 1;

        /* Extract address. */
        Parser->cmdAddr = (((((gctUINT32) (Parser->hi)) >> (0 ? 15:0)) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1)))))) );

        Parser->currentCmdBufferAddr = Parser->currentCmdBufferAddr + 4;
        Parser->skipCount = Parser->cmdSize + Parser->skip;
        break;

     case 0x05:
        Parser->cmdSize   = 4;
        Parser->skipCount = gcmALIGN(Parser->cmdSize, 2);
        break;

    case 0x06:
        Parser->cmdSize   = 5;
        Parser->skipCount = gcmALIGN(Parser->cmdSize, 2);
        break;

    case 0x0C:
        Parser->cmdSize   = 3;
        Parser->skipCount = gcmALIGN(Parser->cmdSize, 2);
        break;

    case 0x09:
        Parser->cmdSize   = 2;
        Parser->cmdAddr   = 0x0F16;
        Parser->skipCount = gcmALIGN(Parser->cmdSize, 2);
        break;

     case 0x04:
        Parser->cmdSize = 1;
        Parser->cmdAddr = 0x0F06;

        cmdRectCount = (((((gctUINT32) (Parser->hi)) >> (0 ? 15:8)) & ((gctUINT32) ((((1 ? 15:8) - (0 ? 15:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:8) - (0 ? 15:8) + 1)))))) );
        cmdDataCount = (((((gctUINT32) (Parser->hi)) >> (0 ? 26:16)) & ((gctUINT32) ((((1 ? 26:16) - (0 ? 26:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:16) - (0 ? 26:16) + 1)))))) );

        Parser->skipCount = gcmALIGN(Parser->cmdSize, 2)
                          + cmdRectCount * 2
                          + gcmALIGN(cmdDataCount, 2);

        Parser->cmdRectCount = cmdRectCount;
        break;

    case 0x03:
        Parser->currentCmdBufferAddr = Parser->currentCmdBufferAddr + 8;
        Parser->skipCount = 0;
        break;

    case 0x02:
        Parser->currentCmdBufferAddr = Parser->currentCmdBufferAddr + 8;
        Parser->skipCount = 0;
        break;

    case 0x07:
        Parser->currentCmdBufferAddr = Parser->currentCmdBufferAddr + 8;
        Parser->skipCount = 0;
        break;

    case 0x08:
        /* Commands after LINK isn't executed, skip them. */
        Parser->stop = gcvTRUE;
        break;

    default:
        /* Unknown command is a risk. */
        Parser->allow = gcvFALSE;
        break;
    }
}

static void
_ParseCommand(
    IN OUT gckPARSER Parser
    )
{
    switch(Parser->cmdOpcode)
    {
    case 0x01:
        _HandleLoadState(Parser);
        break;
    case 0x05:
    case 0x06:
    case 0x0C:
        break;
    case 0x04:
        break;
    default:
        break;
    }

    /* Advance to next command. */
    Parser->currentCmdBufferAddr = Parser->currentCmdBufferAddr
                                 + (Parser->skipCount << 2);
}

gceSTATUS
gckPARSER_Parse(
    IN gckPARSER Parser,
    IN gctUINT8_PTR Buffer,
    IN gctUINT32 Bytes
    )
{
    gckPARSER parser = Parser;
    gctUINT8_PTR end = (gctUINT8_PTR)Buffer + Bytes;

    /* Initialize parser. */
    parser->currentCmdBufferAddr = (gctUINT8_PTR)Buffer;
    parser->skip = 0;
    parser->allow = gcvTRUE;
    parser->stop  = gcvFALSE;

    /* Go through command buffer until reaching the end
    ** or meeting an error. */
    do
    {
        _GetCommand(parser);

        _ParseCommand(parser);
    }
    while ((parser->currentCmdBufferAddr < end)
        && (parser->allow == gcvTRUE)
        && (parser->stop == gcvFALSE)
        );

    if (parser->allow == gcvFALSE)
    {
        /* Error detected. */
        return gcvSTATUS_NOT_SUPPORTED;
    }

    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckPARSER_RegisterCommandHandler
**
**  Register a command handler which will be called when parser get a command.
**
*/
gceSTATUS
gckPARSER_RegisterCommandHandler(
    IN gckPARSER Parser,
    IN gckPARSER_HANDLER Handler
    )
{
    Parser->commandHandler = Handler;

    return gcvSTATUS_OK;
}

gceSTATUS
gckPARSER_Construct(
    IN gckOS Os,
    IN gckPARSER_HANDLER Handler,
    OUT gckPARSER * Parser
    )
{
    gceSTATUS status;
    gckPARSER pointer;

    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(gcsPARSER), (gctPOINTER *)&pointer));

    /* Put it here temp, should have a more general plug-in mechnisam. */
    pointer->commandHandler = Handler;

    *Parser = pointer;

    return gcvSTATUS_OK;

OnError:
    return status;
}

void
gckPARSER_Destroy(
    IN gckOS Os,
    IN gckPARSER Parser
    )
{
    gcmkOS_SAFE_FREE(Os, Parser);
}

/******************************************************************************\
**************************** Hardware States Recorder **************************
\******************************************************************************/

static void
_RecodeState(
    IN gckPARSER_HANDLER Handler,
    IN gctUINT32 Addr,
    IN gctUINT32 Data
    )
{
    gcmkVERIFY_OK(gckRECORDER_UpdateMirror(Handler->private, Addr, Data));
}

static gctUINT
_Previous(
    IN gctUINT Index
    )
{
    if (Index == 0)
    {
        return gcdNUM_RECORDS - 1;
    }

    return Index - 1;
}

static gctUINT
_Next(
    IN gctUINT Index
    )
{
    return (Index + 1) % gcdNUM_RECORDS;
}

gceSTATUS
gckRECORDER_Construct(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    OUT gckRECORDER * Recorder
    )
{
    gceSTATUS status;
    gckCONTEXT context = gcvNULL;
    gckRECORDER recorder = gcvNULL;
    gctSIZE_T mapSize;
    gctUINT i;
    gctBOOL virtualCommandBuffer = Hardware->kernel->virtualCommandBuffer;

    /* TODO: We only need context buffer and state map, it should be able to get without construct a
    ** new context.
    ** Now it is leaked, since we can't free it when command buffer is gone.
    */

    /* MMU is not ready now. */
    Hardware->kernel->virtualCommandBuffer = gcvFALSE;

    gcmkONERROR(gckCONTEXT_Construct(Os, Hardware, 0, &context));

    /* Restore. */
    Hardware->kernel->virtualCommandBuffer = virtualCommandBuffer;

    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(gcsRECORDER), (gctPOINTER *)&recorder));

    gckOS_ZeroMemory(recorder, gcmSIZEOF(gcsRECORDER));

    /* Copy state map. */
    recorder->mirror.maxState = context->maxState;

    mapSize = context->maxState * gcmSIZEOF(gcsSTATE_MAP);

    gcmkONERROR(gckOS_Allocate(Os, mapSize, (gctPOINTER *)&recorder->mirror.map));

    gckOS_MemCopy(recorder->mirror.map, context->map, mapSize);

    /* Copy context buffer. */
    recorder->mirror.bytes = context->totalSize;

    for (i = 0; i < gcdNUM_RECORDS; i++)
    {
        gcmkONERROR(gckOS_Allocate(Os, context->totalSize, (gctPOINTER *)&recorder->mirror.logical[i]));
        gckOS_MemCopy(recorder->mirror.logical[i], context->buffer->logical, context->totalSize);
    }

    for (i = 0; i < gcdNUM_RECORDS; i++)
    {
        /* TODO : Optimize size. */
        gcmkONERROR(gckOS_Allocate(Os, gcdCMD_BUFFER_SIZE, (gctPOINTER *)&recorder->deltas[i].command));
        gcmkONERROR(gckOS_Allocate(Os, context->totalSize, (gctPOINTER *)&recorder->deltas[i].context));
    }

    recorder->index = 0;
    recorder->num   = 0;

    /* Initialize Parser plugin. */
    recorder->recorderHandler.cmd = 0x01;
    recorder->recorderHandler.private = recorder;
    recorder->recorderHandler.function = _RecodeState;

    gcmkONERROR(gckPARSER_Construct(Os, &recorder->recorderHandler, &recorder->parser));

    recorder->os = Os;

    *Recorder = recorder;

    return gcvSTATUS_OK;

OnError:
    if (recorder)
    {
        gckRECORDER_Destory(Os, recorder);
    }

    return status;
}

gceSTATUS
gckRECORDER_Destory(
    IN gckOS Os,
    IN gckRECORDER Recorder
    )
{
    gctUINT i;

    if (Recorder->mirror.map)
    {
        gcmkOS_SAFE_FREE(Os, Recorder->mirror.map);
    }

    for (i = 0; i < gcdNUM_RECORDS; i++)
    {
        if (Recorder->mirror.logical[i])
        {
            gcmkOS_SAFE_FREE(Os, Recorder->mirror.logical[i]);
        }
    }

    for (i = 0; i < gcdNUM_RECORDS; i++)
    {
        if (Recorder->deltas[i].command)
        {
            gcmkOS_SAFE_FREE(Os, Recorder->deltas[i].command);
        }

        if (Recorder->deltas[i].context)
        {
            gcmkOS_SAFE_FREE(Os, Recorder->deltas[i].context);
        }
    }

    if (Recorder->parser)
    {
        gckPARSER_Destroy(Os, Recorder->parser);
    }

    gcmkOS_SAFE_FREE(Os, Recorder);

    return gcvSTATUS_OK;
}

gceSTATUS
gckRECORDER_UpdateMirror(
    IN gckRECORDER Recorder,
    IN gctUINT32 State,
    IN gctUINT32 Data
    )
{
    gctUINT32 index;
    gcsSTATE_MAP_PTR map = Recorder->mirror.map;
    gctUINT32_PTR buffer = Recorder->mirror.logical[Recorder->index];

    if (State >= Recorder->mirror.maxState)
    {
        /* Ignore them just like HW does. */
        return gcvSTATUS_OK;
    }

    index = map[State].index;

    if (index)
    {
        buffer[index] = Data;
    }

    return gcvSTATUS_OK;
}

void
gckRECORDER_AdvanceIndex(
    IN gckRECORDER Recorder,
    IN gctUINT64 CommitStamp
    )
{
    /* Get next record. */
    gctUINT next = (Recorder->index + 1) % gcdNUM_RECORDS;

    /* Record stamp of this commit. */
    Recorder->deltas[Recorder->index].commitStamp = CommitStamp;

    /* Mirror of next record is mirror of this record and delta in next record. */
    gckOS_MemCopy(Recorder->mirror.logical[next],
        Recorder->mirror.logical[Recorder->index], Recorder->mirror.bytes);

    /* Advance to next record. */
    Recorder->index = next;

    Recorder->num = gcmMIN(Recorder->num + 1, gcdNUM_RECORDS - 1);


    /* Reset delta. */
    Recorder->deltas[Recorder->index].commandBytes = 0;
    Recorder->deltas[Recorder->index].contextBytes = 0;
}

void
gckRECORDER_Record(
    IN gckRECORDER Recorder,
    IN gctUINT8_PTR CommandBuffer,
    IN gctUINT32 CommandBytes,
    IN gctUINT8_PTR ContextBuffer,
    IN gctUINT32 ContextBytes
    )
{
    gcsDELTA * delta = &Recorder->deltas[Recorder->index];

    if (CommandBytes != 0xFFFFFFFF)
    {
        gckPARSER_Parse(Recorder->parser, CommandBuffer, CommandBytes);
        gckOS_MemCopy(delta->command, CommandBuffer, CommandBytes);
        delta->commandBytes = CommandBytes;
    }

    if (ContextBytes != 0xFFFFFFFF)
    {
        gckPARSER_Parse(Recorder->parser, ContextBuffer, ContextBytes);
        gckOS_MemCopy(delta->context, ContextBuffer, ContextBytes);
        delta->contextBytes = ContextBytes;
    }
}

void
gckRECORDER_Dump(
    IN gckRECORDER Recorder
    )
{
    gctUINT last = Recorder->index;
    gctUINT previous;
    gctUINT i;
    gcsMIRROR *mirror = &Recorder->mirror;
    gcsDELTA *delta;
    gckOS os = Recorder->os;

    for (i = 0; i < Recorder->num; i++)
    {
        last = _Previous(last);
    }

    for (i = 0; i < Recorder->num; i++)
    {
        delta = &Recorder->deltas[last];

        /* Dump record */
        gcmkPRINT("#[commit %llu]", delta->commitStamp);

        if (delta->commitStamp)
        {
            previous = _Previous(last);

            gcmkPRINT("#[mirror]");
            gckOS_DumpBuffer(os, mirror->logical[previous], mirror->bytes, gceDUMP_BUFFER_CONTEXT, gcvTRUE);
            gcmkPRINT("@[kernel.execute]");
        }

        if (delta->contextBytes)
        {
            gckOS_DumpBuffer(os, delta->context, delta->contextBytes, gceDUMP_BUFFER_CONTEXT, gcvTRUE);
            gcmkPRINT("@[kernel.execute]");
        }

        gckOS_DumpBuffer(os, delta->command, delta->commandBytes, gceDUMP_BUFFER_USER, gcvTRUE);
        gcmkPRINT("@[kernel.execute]");

        last = _Next(last);
    }
}


