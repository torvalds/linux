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


#include "gc_hal.h"
#include "gc_hal_kernel.h"

#if gcdENABLE_VG

#include "gc_hal_kernel_hardware_command_vg.h"

#define _GC_OBJ_ZONE    gcvZONE_COMMAND

/******************************************************************************\
****************************** gckVGCOMMAND API code *****************************
\******************************************************************************/

/*******************************************************************************
**
**  gckVGCOMMAND_InitializeInfo
**
**  Initialize architecture dependent command buffer information.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to the Command object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVGCOMMAND_InitializeInfo(
    IN gckVGCOMMAND Command
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    do
    {
        /* Reset interrupts. */
        Command->info.feBufferInt   = -1;
        Command->info.tsOverflowInt = -1;

        /* Set command buffer attributes. */
        Command->info.addressAlignment = 64;
        Command->info.commandAlignment = 8;

        /* Determine command alignment address mask. */
        Command->info.addressMask = ((((gctUINT32) (Command->info.addressAlignment - 1)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0))) | (((gctUINT32) ((gctUINT32) (0 ) & ((gctUINT32) ((((1 ? 1:0) - (0 ? 1:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:0) - (0 ? 1:0) + 1))))))) << (0 ? 1:0)));

        /* Query the number of bytes needed by the STATE command. */
        gcmkERR_BREAK(gckVGCOMMAND_StateCommand(
            Command, 0x0, gcvNULL, (gctUINT32)~0, 0,
            &Command->info.stateCommandSize
            ));

        /* Query the number of bytes needed by the RESTART command. */
        gcmkERR_BREAK(gckVGCOMMAND_RestartCommand(
            Command, gcvNULL, (gctUINT32)~0, 0,
            &Command->info.restartCommandSize
            ));

        /* Query the number of bytes needed by the FETCH command. */
        gcmkERR_BREAK(gckVGCOMMAND_FetchCommand(
            Command, gcvNULL, (gctUINT32)~0, 0,
            &Command->info.fetchCommandSize
            ));

        /* Query the number of bytes needed by the CALL command. */
        gcmkERR_BREAK(gckVGCOMMAND_CallCommand(
            Command, gcvNULL, (gctUINT32)~0, 0,
            &Command->info.callCommandSize
            ));

        /* Query the number of bytes needed by the RETURN command. */
        gcmkERR_BREAK(gckVGCOMMAND_ReturnCommand(
            Command, gcvNULL,
            &Command->info.returnCommandSize
            ));

        /* Query the number of bytes needed by the EVENT command. */
        gcmkERR_BREAK(gckVGCOMMAND_EventCommand(
            Command, gcvNULL, gcvBLOCK_PIXEL, -1,
            &Command->info.eventCommandSize
            ));

        /* Query the number of bytes needed by the END command. */
        gcmkERR_BREAK(gckVGCOMMAND_EndCommand(
            Command, gcvNULL, -1,
            &Command->info.endCommandSize
            ));

        /* Determine the tail reserve size. */
        Command->info.staticTailSize = gcmMAX(
            Command->info.fetchCommandSize,
            gcmMAX(
                Command->info.returnCommandSize,
                Command->info.endCommandSize
                )
            );

        /* Determine the maximum tail size. */
        Command->info.dynamicTailSize
            = Command->info.staticTailSize
            + Command->info.eventCommandSize * gcvBLOCK_COUNT;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return status. */
    return status;
}

/*******************************************************************************
**
**  gckVGCOMMAND_StateCommand
**
**  Append a STATE command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to an gckVGCOMMAND object.
**
**      gctUINT32 Pipe
**          Harwdare destination pipe.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          STATE command at or gcvNULL to query the size of the command.
**
**      gctUINT32 Address
**          Starting register address of the state buffer.
**          If 'Logical' is gcvNULL, this argument is ignored.
**
**      gctUINT32 Count
**          Number of states in state buffer.
**          If 'Logical' is gcvNULL, this argument is ignored.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the STATE command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the STATE command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_StateCommand(
    IN gckVGCOMMAND Command,
    IN gctUINT32 Pipe,
    IN gctPOINTER Logical,
    IN gctUINT32 Address,
    IN gctUINT32 Count,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Pipe=0x%x Logical=0x%x Address=0x%x Count=0x%x Bytes = 0x%x",
                   Command, Pipe, Logical, Address, Count, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append STATE. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:0) - (0 ? 11:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:0) - (0 ? 11:0) + 1))))))) << (0 ? 11:0))) | (((gctUINT32) ((gctUINT32) (Address) & ((gctUINT32) ((((1 ? 11:0) - (0 ? 11:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:0) - (0 ? 11:0) + 1))))))) << (0 ? 11:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:16) - (0 ? 27:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:16) - (0 ? 27:16) + 1))))))) << (0 ? 27:16))) | (((gctUINT32) ((gctUINT32) (Count) & ((gctUINT32) ((((1 ? 27:16) - (0 ? 27:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:16) - (0 ? 27:16) + 1))))))) << (0 ? 27:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 13:12) - (0 ? 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ? 13:12))) | (((gctUINT32) ((gctUINT32) (Pipe) & ((gctUINT32) ((((1 ? 13:12) - (0 ? 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ? 13:12)));
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the STATE command. */
            *Bytes = 4 * (Count + 1);
        }
    }
    else
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append LOAD_STATE. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (Count) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (Address) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the STATE command. */
            *Bytes = 4 * (Count + 1);
        }
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGCOMMAND_RestartCommand
**
**  Form a RESTART command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to an gckVGCOMMAND object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          RESTART command at or gcvNULL to query the size of the command.
**
**      gctUINT32 FetchAddress
**          The address of another command buffer to be executed by this RESTART
**          command.  If 'Logical' is gcvNULL, this argument is ignored.
**
**      gctUINT FetchCount
**          The number of 64-bit data quantities in another command buffer to
**          be executed by this RESTART command.  If 'Logical' is gcvNULL, this
**          argument is ignored.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the RESTART command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the RESTART command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_RestartCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT FetchCount,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Logical=0x%x FetchAddress=0x%x FetchCount=0x%x Bytes = 0x%x",
                   Command, Logical, FetchAddress, FetchCount, Bytes);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;
            gctUINT32 beginEndMark;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Determine Begin/End flag. */
            beginEndMark = (FetchCount > 0)
                ? ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 24:24) - (0 ? 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ? 24:24))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 24:24) - (0 ? 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ? 24:24)))
                : ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 24:24) - (0 ? 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ? 24:24))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 24:24) - (0 ? 24:24) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 24:24) - (0 ? 24:24) + 1))))))) << (0 ? 24:24)));

            /* Append RESTART. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x9 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:0) - (0 ? 20:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:0) - (0 ? 20:0) + 1))))))) << (0 ? 20:0))) | (((gctUINT32) ((gctUINT32) (FetchCount) & ((gctUINT32) ((((1 ? 20:0) - (0 ? 20:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:0) - (0 ? 20:0) + 1))))))) << (0 ? 20:0)))
                | beginEndMark;

            buffer[1]
                = FetchAddress;
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the RESTART command. */
            *Bytes = 8;
        }
    }
    else
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_NOT_SUPPORTED;
    }


    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGCOMMAND_FetchCommand
**
**  Form a FETCH command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to an gckVGCOMMAND object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          FETCH command at or gcvNULL to query the size of the command.
**
**      gctUINT32 FetchAddress
**          The address of another command buffer to be executed by this FETCH
**          command.  If 'Logical' is gcvNULL, this argument is ignored.
**
**      gctUINT FetchCount
**          The number of 64-bit data quantities in another command buffer to
**          be executed by this FETCH command.  If 'Logical' is gcvNULL, this
**          argument is ignored.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the FETCH command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the FETCH command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_FetchCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT FetchCount,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Logical=0x%x FetchAddress=0x%x FetchCount=0x%x Bytes = 0x%x",
                   Command, Logical, FetchAddress, FetchCount, Bytes);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append FETCH. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x5 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:0) - (0 ? 20:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:0) - (0 ? 20:0) + 1))))))) << (0 ? 20:0))) | (((gctUINT32) ((gctUINT32) (FetchCount) & ((gctUINT32) ((((1 ? 20:0) - (0 ? 20:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:0) - (0 ? 20:0) + 1))))))) << (0 ? 20:0)));

            buffer[1]
                = gcmkFIXADDRESS(FetchAddress);
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the FETCH command. */
            *Bytes = 8;
        }
    }
    else
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append LINK. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (FetchCount) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

            buffer[1]
                = gcmkFIXADDRESS(FetchAddress);
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the LINK command. */
            *Bytes = 8;
        }
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGCOMMAND_CallCommand
**
**  Append a CALL command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to an gckVGCOMMAND object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          CALL command at or gcvNULL to query the size of the command.
**
**      gctUINT32 FetchAddress
**          The address of another command buffer to be executed by this CALL
**          command.  If 'Logical' is gcvNULL, this argument is ignored.
**
**      gctUINT FetchCount
**          The number of 64-bit data quantities in another command buffer to
**          be executed by this CALL command.  If 'Logical' is gcvNULL, this
**          argument is ignored.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the CALL command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the CALL command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_CallCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT FetchCount,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Logical=0x%x FetchAddress=0x%x FetchCount=0x%x Bytes = 0x%x",
                   Command, Logical, FetchAddress, FetchCount, Bytes);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append CALL. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x6 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 20:0) - (0 ? 20:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:0) - (0 ? 20:0) + 1))))))) << (0 ? 20:0))) | (((gctUINT32) ((gctUINT32) (FetchCount) & ((gctUINT32) ((((1 ? 20:0) - (0 ? 20:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 20:0) - (0 ? 20:0) + 1))))))) << (0 ? 20:0)));

            buffer[1]
                = gcmkFIXADDRESS(FetchAddress);
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the CALL command. */
            *Bytes = 8;
        }
    }
    else
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_NOT_SUPPORTED;
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGCOMMAND_ReturnCommand
**
**  Append a RETURN command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to an gckVGCOMMAND object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          RETURN command at or gcvNULL to query the size of the command.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the RETURN command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the RETURN command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_ReturnCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Logical=0x%x  Bytes = 0x%x",
                   Command, Logical, Bytes);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append RETURN. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x7 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)));
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the RETURN command. */
            *Bytes = 8;
        }
    }
    else
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_NOT_SUPPORTED;
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGCOMMAND_EventCommand
**
**  Form an EVENT command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to the Command object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          EVENT command at or gcvNULL to query the size of the command.
**
**      gctINT32 InterruptId
**          The ID of the interrupt to generate.
**          If 'Logical' is gcvNULL, this argument is ignored.
**
**      gceBLOCK Block
**          Block that will generate the interrupt.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the EVENT command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the END command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_EventCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gceBLOCK Block,
    IN gctINT32 InterruptId,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Logical=0x%x Block=0x%x InterruptId=0x%x Bytes = 0x%x",
                   Command, Logical, Block, InterruptId, Bytes);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        typedef struct _gcsEVENTSTATES
        {
            /* Chips before VG21 use these values. */
            gctUINT     eventFromFE;
            gctUINT     eventFromPE;

            /* VG21 chips and later use SOURCE field. */
            gctUINT     eventSource;
        }
        gcsEVENTSTATES;

        static gcsEVENTSTATES states[] =
        {
            /* gcvBLOCK_COMMAND */
            {
                (gctUINT)~0,
                (gctUINT)~0,
                (gctUINT)~0
            },

            /* gcvBLOCK_TESSELLATOR */
            {
                0x0,
                0x1,
                0x10
            },

            /* gcvBLOCK_TESSELLATOR2 */
            {
                0x0,
                0x1,
                0x12
            },

            /* gcvBLOCK_TESSELLATOR3 */
            {
                0x0,
                0x1,
                0x14
            },

            /* gcvBLOCK_RASTER */
            {
                0x0,
                0x1,
                0x07,
            },

            /* gcvBLOCK_VG */
            {
                0x0,
                0x1,
                0x0F
            },

            /* gcvBLOCK_VG2 */
            {
                0x0,
                0x1,
                0x11
            },

            /* gcvBLOCK_VG3 */
            {
                0x0,
                0x1,
                0x13
            },

            /* gcvBLOCK_PIXEL */
            {
                0x0,
                0x1,
                0x07
            },
        };

        /* Verify block ID. */
        gcmkVERIFY_ARGUMENT(gcmIS_VALID_INDEX(Block, states));

        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Verify the event ID. */
            gcmkVERIFY_ARGUMENT(InterruptId >= 0);
            gcmkVERIFY_ARGUMENT(InterruptId <= ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))));

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append EVENT. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x3 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 11:0) - (0 ? 11:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:0) - (0 ? 11:0) + 1))))))) << (0 ? 11:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 11:0) - (0 ? 11:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:0) - (0 ? 11:0) + 1))))))) << (0 ? 11:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 27:16) - (0 ? 27:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:16) - (0 ? 27:16) + 1))))))) << (0 ? 27:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 27:16) - (0 ? 27:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 27:16) - (0 ? 27:16) + 1))))))) << (0 ? 27:16)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 13:12) - (0 ? 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ? 13:12))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 13:12) - (0 ? 13:12) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:12) - (0 ? 13:12) + 1))))))) << (0 ? 13:12)));

            /* Determine chip version. */
            if (Command->vg21)
            {
                /* Get the event source for the block. */
                gctUINT eventSource = states[Block].eventSource;

                /* Supported? */
                if (eventSource == ~0)
                {
                    gcmkFOOTER_NO();
                    return gcvSTATUS_NOT_SUPPORTED;
                }

                buffer[1]
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (InterruptId) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) ((gctUINT32) (eventSource) & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
            }
            else
            {
                /* Get the event source for the block. */
                gctUINT eventFromFE = states[Block].eventFromFE;
                gctUINT eventFromPE = states[Block].eventFromPE;

                /* Supported? */
                if (eventFromFE == ~0)
                {
                    gcmkFOOTER_NO();
                    return gcvSTATUS_NOT_SUPPORTED;
                }

                buffer[1]
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (InterruptId) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) ((gctUINT32) (eventFromFE) & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) ((gctUINT32) (eventFromPE) & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
            }
        }

        if (Bytes != gcvNULL)
        {
            /* Make sure the events are directly supported for the block. */
            if (states[Block].eventSource == ~0)
            {
                gcmkFOOTER_NO();
                return gcvSTATUS_NOT_SUPPORTED;
            }

            /* Return number of bytes required by the END command. */
            *Bytes = 8;
        }
    }
    else
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Verify the event ID. */
            gcmkVERIFY_ARGUMENT(InterruptId >= 0);
            gcmkVERIFY_ARGUMENT(InterruptId <= ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))));

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append EVENT. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            /* Determine event source. */
            if (Block == gcvBLOCK_COMMAND)
            {
                buffer[1]
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (InterruptId) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
            }
            else
            {
                buffer[1]
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (InterruptId) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));
            }
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the EVENT and END commands. */
            *Bytes = 8;
        }
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGCOMMAND_EndCommand
**
**  Form an END command at the specified location in the command buffer.
**
**  INPUT:
**
**      gckVGCOMMAND Command
**          Pointer to the Command object.
**
**      gctPOINTER Logical
**          Pointer to the current location inside the command buffer to append
**          END command at or gcvNULL to query the size of the command.
**
**      gctINT32 InterruptId
**          The ID of the interrupt to generate.
**          If 'Logical' is gcvNULL, this argument will be ignored.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes available for the END command.
**          If 'Logical' is gcvNULL, the value from this argument is ignored.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the END command.  If 'Bytes' is gcvNULL, nothing is returned.
*/
gceSTATUS
gckVGCOMMAND_EndCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctINT32 InterruptId,
    IN OUT gctUINT32 * Bytes
    )
{
    gcmkHEADER_ARG("Command=0x%x Logical=0x%x InterruptId=0x%x Bytes = 0x%x",
                   Command, Logical, InterruptId, Bytes);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->fe20)
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR buffer;

            /* Verify the event ID. */
            gcmkVERIFY_ARGUMENT(InterruptId >= 0);

            /* Cast the buffer pointer. */
            buffer = (gctUINT32_PTR) Logical;

            /* Append END. */
            buffer[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 31:28) - (0 ? 31:28) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:28) - (0 ? 31:28) + 1))))))) << (0 ? 31:28)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (InterruptId) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)));
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the END command. */
            *Bytes = 8;
        }
    }
    else
    {
        if (Logical != gcvNULL)
        {
            gctUINT32_PTR memory;

            /* Verify the event ID. */
            gcmkVERIFY_ARGUMENT(InterruptId >= 0);

            /* Cast the buffer pointer. */
            memory = (gctUINT32_PTR) Logical;

            /* Append EVENT. */
            memory[0]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E01) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            memory[1]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) ((gctUINT32) (InterruptId) & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
                | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 6:6) - (0 ? 6:6) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 6:6) - (0 ? 6:6) + 1))))))) << (0 ? 6:6)));

            /* Append END. */
            memory[2]
                = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x02 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));
        }

        if (Bytes != gcvNULL)
        {
            /* Return number of bytes required by the EVENT and END commands. */
            *Bytes = 16;
        }
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

#endif /* gcdENABLE_VG */

