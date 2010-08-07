/* Cypress West Bridge API header file (cyasmedia.h)
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
## Foundation, Inc., 51 Franklin Street
## Fifth Floor, Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYASMEDIA_H_
#define _INCLUDED_CYASMEDIA_H_

#include "cyas_cplus_start.h"


/* Summary
   Specifies a specific type of media supported by West Bridge

   Description
   The West Bridge device supports five specific types of media
   as storage/IO devices attached to it's S-Port.  This type is
   used to indicate the type of  media being referenced in any
   API call.
*/
typedef enum cy_as_media_type {
	/* Flash NAND memory (may be SLC or MLC) */
	cy_as_media_nand = 0x00,
	/* An SD flash memory device */
	cy_as_media_sd_flash = 0x01,
	/* An MMC flash memory device */
	cy_as_media_mmc_flash = 0x02,
	/* A CE-ATA disk drive */
	cy_as_media_ce_ata = 0x03,
	/* SDIO device. */
	cy_as_media_sdio = 0x04,
	cy_as_media_max_media_value = 0x05

} cy_as_media_type ;

#include "cyas_cplus_end.h"

#endif				/* _INCLUDED_CYASMEDIA_H_ */
