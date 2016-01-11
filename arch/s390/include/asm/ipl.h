/*
 * s390 (re)ipl support
 *
 * Copyright IBM Corp. 2007
 */

#ifndef _ASM_S390_IPL_H
#define _ASM_S390_IPL_H

#include <asm/lowcore.h>
#include <asm/types.h>
#include <asm/cio.h>
#include <asm/setup.h>

#define IPL_PARMBLOCK_ORIGIN	0x2000

#define IPL_PARM_BLK_FCP_LEN (sizeof(struct ipl_list_hdr) + \
			      sizeof(struct ipl_block_fcp))

#define IPL_PARM_BLK0_FCP_LEN (sizeof(struct ipl_block_fcp) + 16)

#define IPL_PARM_BLK_CCW_LEN (sizeof(struct ipl_list_hdr) + \
			      sizeof(struct ipl_block_ccw))

#define IPL_PARM_BLK0_CCW_LEN (sizeof(struct ipl_block_ccw) + 16)

#define IPL_MAX_SUPPORTED_VERSION (0)

#define IPL_PARMBLOCK_START	((struct ipl_parameter_block *) \
				 IPL_PARMBLOCK_ORIGIN)
#define IPL_PARMBLOCK_SIZE	(IPL_PARMBLOCK_START->hdr.len)

struct ipl_list_hdr {
	u32 len;
	u8  reserved1[3];
	u8  version;
	u32 blk0_len;
	u8  pbt;
	u8  flags;
	u16 reserved2;
	u8  loadparm[8];
} __attribute__((packed));

struct ipl_block_fcp {
	u8  reserved1[305-1];
	u8  opt;
	u8  reserved2[3];
	u16 reserved3;
	u16 devno;
	u8  reserved4[4];
	u64 wwpn;
	u64 lun;
	u32 bootprog;
	u8  reserved5[12];
	u64 br_lba;
	u32 scp_data_len;
	u8  reserved6[260];
	u8  scp_data[];
} __attribute__((packed));

#define DIAG308_VMPARM_SIZE	64
#define DIAG308_SCPDATA_SIZE	(PAGE_SIZE - (sizeof(struct ipl_list_hdr) + \
				 offsetof(struct ipl_block_fcp, scp_data)))

struct ipl_block_ccw {
	u8  reserved1[84];
	u16 reserved2 : 13;
	u8  ssid : 3;
	u16 devno;
	u8  vm_flags;
	u8  reserved3[3];
	u32 vm_parm_len;
	u8  nss_name[8];
	u8  vm_parm[DIAG308_VMPARM_SIZE];
	u8  reserved4[8];
} __attribute__((packed));

struct ipl_parameter_block {
	struct ipl_list_hdr hdr;
	union {
		struct ipl_block_fcp fcp;
		struct ipl_block_ccw ccw;
	} ipl_info;
} __attribute__((packed,aligned(4096)));

/*
 * IPL validity flags
 */
extern u32 ipl_flags;
extern u32 dump_prefix_page;

struct dump_save_areas {
	struct save_area_ext **areas;
	int count;
};

extern struct dump_save_areas dump_save_areas;

extern void do_reipl(void);
extern void do_halt(void);
extern void do_poff(void);
extern void ipl_save_parameters(void);
extern void ipl_update_parameters(void);
extern size_t append_ipl_vmparm(char *, size_t);
extern size_t append_ipl_scpdata(char *, size_t);

enum {
	IPL_DEVNO_VALID		= 1,
	IPL_PARMBLOCK_VALID	= 2,
	IPL_NSS_VALID		= 4,
};

enum ipl_type {
	IPL_TYPE_UNKNOWN	= 1,
	IPL_TYPE_CCW		= 2,
	IPL_TYPE_FCP		= 4,
	IPL_TYPE_FCP_DUMP	= 8,
	IPL_TYPE_NSS		= 16,
};

struct ipl_info
{
	enum ipl_type type;
	union {
		struct {
			struct ccw_dev_id dev_id;
		} ccw;
		struct {
			struct ccw_dev_id dev_id;
			u64 wwpn;
			u64 lun;
		} fcp;
		struct {
			char name[NSS_NAME_SIZE + 1];
		} nss;
	} data;
};

extern struct ipl_info ipl_info;
extern void setup_ipl(void);

/*
 * DIAG 308 support
 */
enum diag308_subcode  {
	DIAG308_REL_HSA	= 2,
	DIAG308_IPL	= 3,
	DIAG308_DUMP	= 4,
	DIAG308_SET	= 5,
	DIAG308_STORE	= 6,
};

enum diag308_ipl_type {
	DIAG308_IPL_TYPE_FCP	= 0,
	DIAG308_IPL_TYPE_CCW	= 2,
};

enum diag308_opt {
	DIAG308_IPL_OPT_IPL	= 0x10,
	DIAG308_IPL_OPT_DUMP	= 0x20,
};

enum diag308_flags {
	DIAG308_FLAGS_LP_VALID	= 0x80,
};

enum diag308_vm_flags {
	DIAG308_VM_FLAGS_NSS_VALID	= 0x80,
	DIAG308_VM_FLAGS_VP_VALID	= 0x40,
};

enum diag308_rc {
	DIAG308_RC_OK		= 0x0001,
	DIAG308_RC_NOCONFIG	= 0x0102,
};

extern int diag308(unsigned long subcode, void *addr);
extern void diag308_reset(void);
extern void store_status(void);
extern void lgr_info_log(void);

#endif /* _ASM_S390_IPL_H */
