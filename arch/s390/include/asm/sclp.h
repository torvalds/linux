/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#ifndef _ASM_S390_SCLP_H
#define _ASM_S390_SCLP_H

#include <linux/types.h>
#include <asm/chpid.h>
#include <asm/cpu.h>

#define SCLP_CHP_INFO_MASK_SIZE		32
#define SCLP_MAX_CORES			256

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
	u8 reserved0[2];
	u8 : 3;
	u8 siif : 1;
	u8 sigpif : 1;
	u8 : 3;
	u8 reserved2[10];
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
	unsigned int ibc;
	unsigned int mtid;
	unsigned int mtid_cp;
	unsigned int mtid_prev;
	unsigned long long rzm;
	unsigned long long rnmax;
	unsigned long long hamax;
	unsigned int max_cores;
	unsigned long hsa_size;
	unsigned long long facilities;
};
extern struct sclp_info sclp;

int sclp_get_core_info(struct sclp_core_info *info);
int sclp_core_configure(u8 core);
int sclp_core_deconfigure(u8 core);
int sclp_sdias_blk_count(void);
int sclp_sdias_copy(void *dest, int blk_num, int nr_blks);
int sclp_chp_configure(struct chp_id chpid);
int sclp_chp_deconfigure(struct chp_id chpid);
int sclp_chp_read_info(struct sclp_chp_info *info);
void sclp_get_ipl_info(struct sclp_ipl_info *info);
int sclp_pci_configure(u32 fid);
int sclp_pci_deconfigure(u32 fid);
int memcpy_hsa(void *dest, unsigned long src, size_t count, int mode);
void sclp_early_detect(void);
long _sclp_print_early(const char *);

#endif /* _ASM_S390_SCLP_H */
