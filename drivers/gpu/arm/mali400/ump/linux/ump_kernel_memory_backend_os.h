/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2010 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_kernel_memory_backend_os.h
 */

#ifndef __UMP_KERNEL_MEMORY_BACKEND_OS_H__
#define __UMP_KERNEL_MEMORY_BACKEND_OS_H__

#include "ump_kernel_memory_backend.h"

ump_memory_backend * ump_os_memory_backend_create(const int max_allocation);

#endif /* __UMP_KERNEL_MEMORY_BACKEND_OS_H__ */

