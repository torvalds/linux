/*
 * Aic94xx SAS/SATA driver sequencer interface header file.
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

#ifndef _AIC94XX_SEQ_H_
#define _AIC94XX_SEQ_H_

#define CSEQ_NUM_VECS	3
#define LSEQ_NUM_VECS	11

#define SAS_RAZOR_SEQUENCER_FW_FILE "aic94xx-seq.fw"

/* Note:  All quantites in the sequencer file are little endian */
struct sequencer_file_header {
	/* Checksum of the entire contents of the sequencer excluding
	 * these four bytes */
	u32	csum;
	/* numeric major version */
	u32	major;
	/* numeric minor version */
	u32	minor;
	/* version string printed by driver */
	char	version[16];
	u32	cseq_table_offset;
	u32	cseq_table_size;
	u32	lseq_table_offset;
	u32	lseq_table_size;
	u32	cseq_code_offset;
	u32	cseq_code_size;
	u32	lseq_code_offset;
	u32	lseq_code_size;
	u16	mode2_task;
	u16	cseq_idle_loop;
	u16	lseq_idle_loop;
} __attribute__((packed));

#ifdef __KERNEL__
int asd_pause_cseq(struct asd_ha_struct *asd_ha);
int asd_unpause_cseq(struct asd_ha_struct *asd_ha);
int asd_pause_lseq(struct asd_ha_struct *asd_ha, u8 lseq_mask);
int asd_unpause_lseq(struct asd_ha_struct *asd_ha, u8 lseq_mask);
int asd_init_seqs(struct asd_ha_struct *asd_ha);
int asd_start_seqs(struct asd_ha_struct *asd_ha);

void asd_update_port_links(struct asd_ha_struct *asd_ha, struct asd_phy *phy);
#endif

#endif
