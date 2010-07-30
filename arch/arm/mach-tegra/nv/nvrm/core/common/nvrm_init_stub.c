/*
 * Copyright (c) 2009 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvidlcmd.h"
#include "nvrm_init.h"

void NvRmClose(NvRmDeviceHandle hDevice)
{
}

NvError NvRmOpenNew(NvRmDeviceHandle *pHandle)
{
    *pHandle = (void *)1;
    return NvSuccess;
}

void NvRmInit(NvRmDeviceHandle *pHandle)
{
}

NvError NvRmOpen(NvRmDeviceHandle *pHandle, NvU32 DeviceId)
{
    return NvRmOpenNew(pHandle);
}
