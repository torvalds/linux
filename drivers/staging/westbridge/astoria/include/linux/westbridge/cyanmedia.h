/* Cypress West Bridge API header file (cyanmedia.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYANMEDIA_H_
#define _INCLUDED_CYANMEDIA_H_

#include "cyas_cplus_start.h"

/* Summary
   Specifies a specific type of media supported by West Bridge

   Description
   The West Bridge device supports five specific types
   of media as storage/IO devices attached to it's S-Port.  This
   type is used to indicate the type of media being referenced in
   any API call.
*/
#include "cyasmedia.h"

/* Flash NAND memory (may be SLC or MLC) */
#define cy_an_media_nand cy_as_media_nand

/* An SD flash memory device */
#define cy_an_media_sd_flash cy_as_media_sd_flash

/* An MMC flash memory device */
#define cy_an_media_mmc_flash cy_as_media_mmc_flash

/* A CE-ATA disk drive */
#define cy_an_media_ce_ata cy_as_media_ce_ata

 /* SDIO device. */
#define cy_an_media_sdio cy_as_media_sdio
#define cy_an_media_max_media_value \
	cy_as_media_max_media_value

typedef cy_as_media_type cy_an_media_type;

#include "cyas_cplus_end.h"

#endif				/* _INCLUDED_CYANMEDIA_H_ */
