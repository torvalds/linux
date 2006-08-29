/*
 * Aic94xx SAS/SATA driver dump header file.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This file is part of the aic94xx driver.
 *
 * The aic94xx driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * The aic94xx driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the aic94xx driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef _AIC94XX_DUMP_H_
#define _AIC94XX_DUMP_H_

#ifdef ASD_DEBUG

void asd_dump_ddb_0(struct asd_ha_struct *asd_ha);
void asd_dump_target_ddb(struct asd_ha_struct *asd_ha, u16 site_no);
void asd_dump_scb_sites(struct asd_ha_struct *asd_ha);
void asd_dump_seq_state(struct asd_ha_struct *asd_ha, u8 lseq_mask);
void asd_dump_frame_rcvd(struct asd_phy *phy,
			 struct done_list_struct *dl);
void asd_dump_scb_list(struct asd_ascb *ascb, int num);
#else /* ASD_DEBUG */

static inline void asd_dump_ddb_0(struct asd_ha_struct *asd_ha) { }
static inline void asd_dump_target_ddb(struct asd_ha_struct *asd_ha,
				     u16 site_no) { }
static inline void asd_dump_scb_sites(struct asd_ha_struct *asd_ha) { }
static inline void asd_dump_seq_state(struct asd_ha_struct *asd_ha,
				      u8 lseq_mask) { }
static inline void asd_dump_frame_rcvd(struct asd_phy *phy,
				       struct done_list_struct *dl) { }
static inline void asd_dump_scb_list(struct asd_ascb *ascb, int num) { }
#endif /* ASD_DEBUG */

#endif /* _AIC94XX_DUMP_H_ */
