/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
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
*    Copyright (C) 2014 - 2015 Vivante Corporation
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


#include "gc_hal_kernel_linux.h"
#include <linux/slab.h>

#include "tee_client_api.h"

#define _GC_OBJ_ZONE gcvZONE_OS

#define GPU3D_UUID   { 0xcc9f80ea, 0xa836, 0x11e3, { 0x9b, 0x07, 0x78, 0x2b, 0xcb, 0x5c, 0xf3, 0xe3 } }

static const TEEC_UUID gpu3d_uuid = GPU3D_UUID;
TEEC_Context teecContext;

typedef struct _gcsSecurityChannel {
    gckOS               os;
    TEEC_Session        session;
    int *               virtual;
    TEEC_SharedMemory   inputBuffer;
    gctUINT32           bytes;
    gctPOINTER          mutex;
} gcsSecurityChannel;

TEEC_SharedMemory *
gpu3d_allocate_secure_mem(
    gckOS Os,
    unsigned int size
    )
{
    TEEC_Result result;
    TEEC_Context *context = &teecContext;
    TEEC_SharedMemory *shm = NULL;
    void *handle = NULL;
    gctPHYS_ADDR_T phyAddr;
    gceSTATUS status;
    gctSIZE_T bytes = size;

    shm = kmalloc(sizeof(TEEC_SharedMemory), GFP_KERNEL);

    if (NULL == shm)
    {
        return NULL;
    }

    memset(shm, 0, sizeof(TEEC_SharedMemory));

    status = gckOS_AllocatePagedMemoryEx(
                Os,
                gcvALLOC_FLAG_SECURITY,
                bytes,
                gcvNULL,
                (gctPHYS_ADDR *)&handle);

    if (gcmIS_ERROR(status))
    {
         kfree(shm);
         return NULL;
    }

    status = gckOS_PhysicalToPhysicalAddress(
                Os,
                handle,
                &phyAddr);

    if (gcmIS_ERROR(status))
    {
         kfree(shm);
         return NULL;
    }

    /* record the handle into shm->user_data */
    shm->userdata = handle;

    /* [b] Bulk input buffer. */
    shm->size = size;
    shm->flags = TEEC_MEM_INPUT;

    /* Use TEE Client API to register the underlying memory buffer. */
    shm->phyAddr = (void *)(gctUINT32)phyAddr;

    result = TEEC_RegisterSharedMemory(
            context,
            shm);

    if (result != TEEC_SUCCESS)
    {
        gckOS_FreePagedMemory(Os, (gctPHYS_ADDR)handle, shm->size);
        kfree(shm);
        return NULL;
    }

    return shm;
}

void gpu3d_release_secure_mem(
    gckOS Os,
    void *shm_handle
    )
{
    TEEC_SharedMemory *shm = shm_handle;
    void * handle;

    if (!shm)
    {
        return;
    }

    handle = shm->userdata;

    TEEC_ReleaseSharedMemory(shm);
    gckOS_FreePagedMemory(Os, (gctPHYS_ADDR)handle, shm->size);

    kfree(shm);

    return;
}

static TEEC_Result gpu3d_session_callback(
    TEEC_Session*   session,
    uint32_t    commandID,
    TEEC_Operation* operation,
    void*   userdata
    )
{
    gcsSecurityChannel *channel = userdata;

    if (channel == gcvNULL)
    {
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    switch(commandID)
    {
        case gcvTA_CALLBACK_ALLOC_SECURE_MEM:
        {
            uint32_t size = operation->params[0].value.a;
            TEEC_SharedMemory *shm = NULL;

            shm = gpu3d_allocate_secure_mem(channel->os, size);
            if (shm == NULL)
            {
                return TEEC_ERROR_OUT_OF_MEMORY;
            }

            /* use the value to save the pointer in client side */
            operation->params[0].value.a = (uint32_t)shm;
            operation->params[0].value.b = (uint32_t)shm->phyAddr;

            break;
        }
        case gcvTA_CALLBACK_FREE_SECURE_MEM:
        {
            TEEC_SharedMemory *shm = (TEEC_SharedMemory *)operation->params[0].value.a;

            gpu3d_release_secure_mem(channel->os, shm);
            break;
        }
        default:
            break;
    }

    return TEEC_SUCCESS;
}

gceSTATUS
gckOS_OpenSecurityChannel(
    IN gckOS Os,
    IN gceCORE GPU,
    OUT gctUINT32 *Channel
    )
{
    gceSTATUS status;
    TEEC_Result result;
    static bool initialized = gcvFALSE;
    gcsSecurityChannel *channel = gcvNULL;

    TEEC_Operation operation = {0};

    /* Connect to TEE. */
    if (initialized == gcvFALSE)
    {
        result = TEEC_InitializeContext(NULL, &teecContext);

        if (result != TEEC_SUCCESS) {
            gcmkONERROR(gcvSTATUS_CHIP_NOT_READY);
        }

        initialized = gcvTRUE;
    }

    /* Construct channel. */
    gcmkONERROR(
        gckOS_Allocate(Os, gcmSIZEOF(*channel), (gctPOINTER *)&channel));

    gckOS_ZeroMemory(channel, gcmSIZEOF(gcsSecurityChannel));

    channel->os = Os;

    gcmkONERROR(gckOS_CreateMutex(Os, &channel->mutex));

    /* Allocate shared memory for passing gcTA_INTERFACE. */
    channel->bytes = gcmSIZEOF(gcsTA_INTERFACE);
    channel->virtual = kmalloc(channel->bytes, GFP_KERNEL | __GFP_NOWARN);

    if (!channel->virtual)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    channel->inputBuffer.size    = channel->bytes;
    channel->inputBuffer.flags   = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
    channel->inputBuffer.phyAddr = (void *)virt_to_phys(channel->virtual);

    result = TEEC_RegisterSharedMemory(&teecContext, &channel->inputBuffer);

    if (result != TEEC_SUCCESS)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    operation.paramTypes = TEEC_PARAM_TYPES(
            TEEC_VALUE_INPUT,
            TEEC_NONE,
            TEEC_NONE,
            TEEC_NONE);

    operation.params[0].value.a = GPU;

    /* Open session with TEE application. */
    result = TEEC_OpenSession(
                &teecContext,
                &channel->session,
                &gpu3d_uuid,
                TEEC_LOGIN_USER,
                NULL,
                &operation,
                NULL);

    /* Prepare callback. */
    TEEC_RegisterCallback(&channel->session, gpu3d_session_callback, channel);

    *Channel = (gctUINT32)channel;

    return gcvSTATUS_OK;

OnError:
    if (channel)
    {
        if (channel->virtual)
        {
        }

        if (channel->mutex)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(Os, channel->mutex));
        }

        gcmkVERIFY_OK(gckOS_Free(Os, channel));
    }

    return status;
}

gceSTATUS
gckOS_CloseSecurityChannel(
    IN gctUINT32 Channel
    )
{
    /* TODO . */
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_CallSecurityService(
    IN gctUINT32 Channel,
    IN gcsTA_INTERFACE *Interface
    )
{
    gceSTATUS status;
    TEEC_Result result;
    gcsSecurityChannel *channel = (gcsSecurityChannel *)Channel;
    TEEC_Operation operation = {0};

    gcmkHEADER();
    gcmkVERIFY_ARGUMENT(Channel != 0);

    gckOS_AcquireMutex(channel->os, channel->mutex, gcvINFINITE);

    gckOS_MemCopy(channel->virtual, Interface, channel->bytes);

    operation.paramTypes = TEEC_PARAM_TYPES(
            TEEC_MEMREF_PARTIAL_INPUT,
            TEEC_NONE,
            TEEC_NONE,
            TEEC_NONE);

    /* Note: we use the updated size in the MemRef output by the encryption. */
    operation.params[0].memref.parent = &channel->inputBuffer;
    operation.params[0].memref.offset = 0;
    operation.params[0].memref.size = sizeof(gcsTA_INTERFACE);
    operation.started = true;

    /* Start the commit command within the TEE application. */
    result = TEEC_InvokeCommand(
            &channel->session,
            gcvTA_COMMAND_DISPATCH,
            &operation,
            NULL);

    gckOS_MemCopy(Interface, channel->virtual, channel->bytes);

    gckOS_ReleaseMutex(channel->os, channel->mutex);

    if (result != TEEC_SUCCESS)
    {
        gcmkONERROR(gcvSTATUS_GENERIC_IO);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_InitSecurityChannel(
    IN gctUINT32 Channel
    )
{
    gceSTATUS status;
    TEEC_Result result;
    gcsSecurityChannel *channel = (gcsSecurityChannel *)Channel;
    TEEC_Operation operation = {0};

    gcmkHEADER();
    gcmkVERIFY_ARGUMENT(Channel != 0);

    operation.paramTypes = TEEC_PARAM_TYPES(
            TEEC_MEMREF_PARTIAL_INPUT,
            TEEC_NONE,
            TEEC_NONE,
            TEEC_NONE);

    /* Note: we use the updated size in the MemRef output by the encryption. */
    operation.params[0].memref.parent = &channel->inputBuffer;
    operation.params[0].memref.offset = 0;
    operation.params[0].memref.size = gcmSIZEOF(gcsTA_INTERFACE);
    operation.started = true;

    /* Start the commit command within the TEE application. */
    result = TEEC_InvokeCommand(
            &channel->session,
            gcvTA_COMMAND_INIT,
            &operation,
            NULL);

    if (result != TEEC_SUCCESS)
    {
        gcmkONERROR(gcvSTATUS_GENERIC_IO);
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
