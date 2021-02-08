/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#ifndef _ASM_S390_SCLP_H
#define _ASM_S390_SCLP_H

#include <linux/types.h>

#define SCLP_CHP_INFO_MASK_SIZE		32
#define EARLY_SCCB_SIZE		PAGE_SIZE
#define SCLP_MAX_CORES		512
/* 144 + 16 * SCLP_MAX_CORES + 2 * (SCLP_MAX_CORES - 1) */
#define EXT_SCCB_READ_SCP	(3 * PAGE_SIZE)
/* 24 + 16 * SCLP_MAX_CORES */
#define EXT_SCCB_READ_CPU	(3 * PAGE_SIZE)

#ifndef __ASSEMBLY__
#include <asm/chpid.h>
#include <asm/cpu.h>

struct sclp_chp_info {
	u8 recognized[SCLP_CHP_INFO_MASK_SIZE];
	u8 standby[SCLP_CHP_INFO_MASK_SIZE];
	u8 configured[SCLP_CHP_INFO_MASK_SIZE];
};

#define LOADPARM_LEN 8

struct sclp_ipl_info {
	int is_valid;
	int has_dump;
	char loadparm[LOADPARM_LEN];
};

struct sclp_core_entry {
	u8 core_id;
	u8 reserved0;
	u8 : 4;
	u8 sief2 : 1;
	u8 skey : 1;
	u8 : 2;
	u8 : 2;
	u8 gpere : 1;
	u8 siif : 1;
	u8 sigpif : 1;
	u8 : 3;
	u8 reserved2[3];
	u8 : 2;
	u8 ib : 1;
	u8 cei : 1;
	u8 : 4;
	u8 reserved3[6];
	u8 type;
	u8 reserved1;
} __attribute__((packed));

struct sclp_core_info {
	unsigned int configured;
	unsigned int standby;
	unsigned int combined;
	struct sclp_core_entry core[SCLP_MAX_CORES];
};

struct sclp_info {
	unsigned char has_linemode : 1;
	unsigned char has_vt220 : 1;
	unsigned char has_siif : 1;
	unsigned char has_sigpif : 1;
	unsigned char has_core_type : 1;
	unsigned char has_sprp : 1;
	unsigned char has_hvs : 1;
	unsigned char has_esca : 1;
	unsigned char has_sief2 : 1;
	unsigned char has_64bscao : 1;
	unsigned char has_gpere : 1;
	unsigned char has_cmma : 1;
	unsigned char has_gsls : 1;
	unsigned char has_ib : 1;
	unsigned char has_cei : 1;
	unsigned char has_pfmfi : 1;
	unsigned char has_ibs : 1;
	unsigned char has_skey : 1;
	unsigned char has_kss : 1;
	unsigned char has_gisaf : 1;
	unsigned char has_diag318 : 1;
	unsigned char has_sipl : 1;
	unsigned char has_dirq : 1;
	unsigned int ibc;
	unsigned int mtid;
	unsigned int mtid_cp;
	unsigned int mtid_prev;
	unsigned long rzm;
	unsigned long rnmax;
	unsigned long hamax;
	unsigned int max_cores;
	unsigned long hsa_size;
	unsigned long facilities;
	unsigned int hmfai;
};
extern struct sclp_info sclp;

struct zpci_report_error_header {
	u8 version;	/* Interface version byte */
	u8 action;	/* Action qualifier byte
			 * 0: Adapter Reset Request
			 * 1: Deconfigure and repair action requested
			 *	(OpenCrypto Problem Call Home)
			 * 2: Informational Report
			 *	(OpenCrypto Successful Diagnostics Execution)
			 */
	u16 length;	/* Length of Subsequent Data (up to 4K – SCLP header */
	u8 data[0];	/* Subsequent Data passed verbatim to SCLP ET 24 */
} __packed;

extern char *sclp_early_sccb;

void sclp_early_adjust_va(void);
void sclp_early_set_buffer(void *sccb);
int sclp_early_read_info(void);
int sclp_early_read_storage_info(void);
int sclp_early_get_core_info(struct sclp_core_info *info);
void sclp_early_get_ipl_info(struct sclp_ipl_info *info);
void sclp_early_detect(void);
void sclp_early_printk(const char *s);
void __sclp_early_printk(const char *s, unsigned int len);

int sclp_early_get_memsize(unsigned long *mem);
int sclp_early_get_hsa_size(unsigned long *hsa_size);
int _sclp_get_core_info(struct sclp_core_info *info);
int sclp_core_configure(u8 core);
int sclp_core_deconfigure(u8 core);
int sclp_sdias_blk_count(void);
int sclp_sdias_copy(void *dest, int blk_num, int nr_blks);
int sclp_chp_configure(struct chp_id chpid);
int sclp_chp_deconfigure(struct chp_id chpid);
int sclp_chp_read_info(struct sclp_chp_info *info);
int sclp_pci_configure(u32 fid);
int sclp_pci_deconfigure(u32 fid);
int sclp_ap_configure(u32 apid);
int sclp_ap_deconfigure(u32 apid);
int sclp_pci_report(struct zpci_report_error_header *report, u32 fh, u32 fid);
int memcpy_hsa_kernel(void *dest, unsigned long src, size_t count);
int memcpy_hsa_user(void __user *dest, unsigned long src, size_t count);
void sclp_ocf_cpc_name_copy(char *dst);

static inline int sclp_get_core_info(struct sclp_core_info *info, int early)
{
	if (early)
		return sclp_early_get_core_info(info);
	return _sclp_get_core_info(info);
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_SCLP_H */
