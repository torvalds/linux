/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_graphics_private.h
 *
 *
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef NVRM_GRAPHICS_PRIVATE_H
#define NVRM_GRAPHICS_PRIVATE_H

#define NVRM_TRANSPORT_IN_KERNEL 1

/**
 * Initialize all graphics stuff
 *
 * @param hDevice The RM instance
 */
NvError
NvRmGraphicsOpen( NvRmDeviceHandle rm );

/**
 * Deinitialize all graphics stuff
 *
 * @param hDevice The RM instance
 */
void
NvRmGraphicsClose( NvRmDeviceHandle rm );

/**
 * Initialize the channels.
 *
 * @param hDevice The RM instance
 */
NvError
NvRmPrivChannelInit( NvRmDeviceHandle hDevice );

/**
 * Deinitialize the channels.
 *
 * @param hDevice The RM instance
 */
void
NvRmPrivChannelDeinit( NvRmDeviceHandle hDevice );

/**
 * Initialize the graphics host, including interrupts.
 */
void
NvRmPrivHostInit( NvRmDeviceHandle rm );

void
NvRmPrivHostShutdown( NvRmDeviceHandle rm );

#if (NVRM_TRANSPORT_IN_KERNEL == 0)
NvError
NvRmPrivTransportInit(NvRmDeviceHandle hRmDevice);

void
NvRmPrivTransportDeInit(NvRmDeviceHandle hRmDevice);
#endif

#endif
