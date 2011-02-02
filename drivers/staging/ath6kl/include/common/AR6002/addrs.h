//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
//
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef __ADDRS_H__
#define __ADDRS_H__

/*
 * Special AR6002 Addresses that may be needed by special
 * applications (e.g. ART) on the Host as well as Target.
 */

#if defined(AR6002_REV2)
#define AR6K_RAM_START 0x00500000
#define TARG_RAM_OFFSET(vaddr) ((u32)(vaddr) & 0xfffff)
#define TARG_RAM_SZ (184*1024)
#define TARG_ROM_SZ (80*1024)
#endif
#if defined(AR6002_REV4) || defined(AR6003)
#define AR6K_RAM_START 0x00540000
#define TARG_RAM_OFFSET(vaddr) (((u32)(vaddr) & 0xfffff) - 0x40000)
#define TARG_RAM_SZ (256*1024)
#define TARG_ROM_SZ (256*1024)
#endif

#define AR6002_BOARD_DATA_SZ 768
#define AR6002_BOARD_EXT_DATA_SZ 0
#define AR6003_BOARD_DATA_SZ 1024
#define AR6003_BOARD_EXT_DATA_SZ 768

#define AR6K_RAM_ADDR(byte_offset) (AR6K_RAM_START+(byte_offset))
#define TARG_RAM_ADDRS(byte_offset) AR6K_RAM_ADDR(byte_offset)

#define AR6K_ROM_START 0x004e0000
#define TARG_ROM_OFFSET(vaddr) (((u32)(vaddr) & 0x1fffff) - 0xe0000)
#define AR6K_ROM_ADDR(byte_offset) (AR6K_ROM_START+(byte_offset))
#define TARG_ROM_ADDRS(byte_offset) AR6K_ROM_ADDR(byte_offset)

/*
 * At this ROM address is a pointer to the start of the ROM DataSet Index.
 * If there are no ROM DataSets, there's a 0 at this address.
 */
#define ROM_DATASET_INDEX_ADDR          (TARG_ROM_ADDRS(TARG_ROM_SZ)-8)
#define ROM_MBIST_CKSUM_ADDR            (TARG_ROM_ADDRS(TARG_ROM_SZ)-4)

/*
 * The API A_BOARD_DATA_ADDR() is the proper way to get a read pointer to
 * board data.
 */

/* Size of Board Data, in bytes */
#if defined(AR6002_REV4) || defined(AR6003)
#define BOARD_DATA_SZ AR6003_BOARD_DATA_SZ
#else
#define BOARD_DATA_SZ AR6002_BOARD_DATA_SZ
#endif


/*
 * Constants used by ASM code to access fields of host_interest_s,
 * which is at a fixed location in RAM.
 */
#if defined(AR6002_REV4) || defined(AR6003)
#define HOST_INTEREST_FLASH_IS_PRESENT_ADDR  (AR6K_RAM_START + 0x60c)
#else
#define HOST_INTEREST_FLASH_IS_PRESENT_ADDR  (AR6K_RAM_START + 0x40c)
#endif
#define FLASH_IS_PRESENT_TARGADDR       HOST_INTEREST_FLASH_IS_PRESENT_ADDR

#endif /* __ADDRS_H__ */



