/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Aic94xx SAS/SATA driver sequencer interface header file.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 */

#ifndef _AIC94XX_SEQ_H_
#define _AIC94XX_SEQ_H_

#define CSEQ_NUM_VECS	3
#define LSEQ_NUM_VECS	11

#define SAS_RAZOR_SEQUENCER_FW_FILE "aic94xx-seq.fw"
#define SAS_RAZOR_SEQUENCER_FW_MAJOR	1

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
int asd_init_seqs(struct asd_ha_struct *asd_ha);
int asd_start_seqs(struct asd_ha_struct *asd_ha);
int asd_release_firmware(void);

void asd_update_port_links(struct asd_ha_struct *asd_ha, struct asd_phy *phy);
#endif

#endif
