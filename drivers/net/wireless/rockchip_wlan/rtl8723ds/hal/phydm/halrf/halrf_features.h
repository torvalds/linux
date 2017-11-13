/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#ifndef	__HALRF_FEATURES_H__
#define __HALRF_FEATURES

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	#define	CONFIG_HALRF_POWERTRACKING	1

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	#define	CONFIG_HALRF_POWERTRACKING	1

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	#define	CONFIG_HALRF_POWERTRACKING	1

#endif

#endif
