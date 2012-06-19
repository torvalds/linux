/*
 * SDIO CIS definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SDIO_CIS_H
#define _SDIO_CIS_H

#define CISTPL_NULL     0x00
#define CISTPL_CHECKSUM 0x10
#define CISTPL_VERS_1   0x15
#define CISTPL_ALTSTR   0x16
#define CISTPL_MANFID   0x20
#  define CISTPL_MANFID_SIZE 0x04
#define CISTPL_FUNCID   0x21
#define CISTPL_FUNCE    0x22
#define CISTPL_SDIO_STD 0x91
#define CISTPL_SDIO_EXT 0x92
#define CISTPL_END      0xff
#define CISTPL_FUNCE  0x22
#  define CISTPL_FUNCE_00_SIZE 0x04
#  define CISTPL_FUNCE_01_SIZE 0x2a

#endif /* #ifndef _SDIO_CIS_H */
