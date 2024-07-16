/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RTL8712_EFUSE_H__
#define __RTL8712_EFUSE_H__

#include "osdep_service.h"

#define _REPEAT_THRESHOLD_	3

#define EFUSE_MAX_SIZE		512
#define EFUSE_MAP_MAX_SIZE	128

#define PGPKG_MAX_WORDS	4
#define PGPKT_DATA_SIZE	8 /* PGPKG_MAX_WORDS*2; BYTES sizeof(u8)*8*/
#define MAX_PGPKT_SIZE	9 /* 1 + PGPKT_DATA_SIZE; header + 2 * 4 words (BYTES)*/

#define GET_EFUSE_OFFSET(header)	((header & 0xF0) >> 4)
#define GET_EFUSE_WORD_EN(header)	(header & 0x0F)
#define MAKE_EFUSE_HEADER(offset, word_en)	((((offset) & 0x0F) << 4) | \
						((word_en) & 0x0F))
/*--------------------------------------------------------------------------*/
struct PGPKT_STRUCT {
	u8 offset;
	u8 word_en;
	u8 data[PGPKT_DATA_SIZE];
};
/*--------------------------------------------------------------------------*/
u8 r8712_efuse_reg_init(struct _adapter *padapter);
void r8712_efuse_reg_uninit(struct _adapter *padapter);
u16 r8712_efuse_get_current_size(struct _adapter *padapter);
int r8712_efuse_get_max_size(struct _adapter *padapter);
void r8712_efuse_change_max_size(struct _adapter *padapter);
u8 r8712_efuse_pg_packet_read(struct _adapter *padapter,
			      u8 offset, u8 *data);
u8 r8712_efuse_pg_packet_write(struct _adapter *padapter,
			       const u8 offset, const u8 word_en,
			       const u8 *data);
u8 r8712_efuse_access(struct _adapter *padapter, u8 bRead,
		      u16 start_addr, u16 cnts, u8 *data);
u8 r8712_efuse_map_read(struct _adapter *padapter, u16 addr,
			u16 cnts, u8 *data);
u8 r8712_efuse_map_write(struct _adapter *padapter, u16 addr,
			 u16 cnts, u8 *data);
#endif
