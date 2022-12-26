/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_S390_UAPI_IPL_H
#define _ASM_S390_UAPI_IPL_H

#include <linux/types.h>

/* IPL Parameter List header */
struct ipl_pl_hdr {
	__u32 len;
	__u8  flags;
	__u8  reserved1[2];
	__u8  version;
} __packed;

#define IPL_PL_FLAG_IPLPS	0x80
#define IPL_PL_FLAG_SIPL	0x40
#define IPL_PL_FLAG_IPLSR	0x20

/* IPL Parameter Block header */
struct ipl_pb_hdr {
	__u32 len;
	__u8  pbt;
} __packed;

/* IPL Parameter Block types */
enum ipl_pbt {
	IPL_PBT_FCP = 0,
	IPL_PBT_SCP_DATA = 1,
	IPL_PBT_CCW = 2,
	IPL_PBT_ECKD = 3,
	IPL_PBT_NVME = 4,
};

/* IPL Parameter Block 0 with common fields */
struct ipl_pb0_common {
	__u32 len;
	__u8  pbt;
	__u8  flags;
	__u8  reserved1[2];
	__u8  loadparm[8];
	__u8  reserved2[84];
} __packed;

#define IPL_PB0_FLAG_LOADPARM	0x80

/* IPL Parameter Block 0 for FCP */
struct ipl_pb0_fcp {
	__u32 len;
	__u8  pbt;
	__u8  reserved1[3];
	__u8  loadparm[8];
	__u8  reserved2[304];
	__u8  opt;
	__u8  reserved3[3];
	__u8  cssid;
	__u8  reserved4[1];
	__u16 devno;
	__u8  reserved5[4];
	__u64 wwpn;
	__u64 lun;
	__u32 bootprog;
	__u8  reserved6[12];
	__u64 br_lba;
	__u32 scp_data_len;
	__u8  reserved7[260];
	__u8  scp_data[];
} __packed;

#define IPL_PB0_FCP_OPT_IPL	0x10
#define IPL_PB0_FCP_OPT_DUMP	0x20

/* IPL Parameter Block 0 for NVMe */
struct ipl_pb0_nvme {
	__u32 len;
	__u8  pbt;
	__u8  reserved1[3];
	__u8  loadparm[8];
	__u8  reserved2[304];
	__u8  opt;
	__u8  reserved3[3];
	__u32 fid;
	__u8 reserved4[12];
	__u32 nsid;
	__u8 reserved5[4];
	__u32 bootprog;
	__u8 reserved6[12];
	__u64 br_lba;
	__u32 scp_data_len;
	__u8  reserved7[260];
	__u8  scp_data[];
} __packed;

#define IPL_PB0_NVME_OPT_IPL	0x10
#define IPL_PB0_NVME_OPT_DUMP	0x20

/* IPL Parameter Block 0 for CCW */
struct ipl_pb0_ccw {
	__u32 len;
	__u8  pbt;
	__u8  flags;
	__u8  reserved1[2];
	__u8  loadparm[8];
	__u8  reserved2[84];
	__u16 reserved3 : 13;
	__u8  ssid : 3;
	__u16 devno;
	__u8  vm_flags;
	__u8  reserved4[3];
	__u32 vm_parm_len;
	__u8  nss_name[8];
	__u8  vm_parm[64];
	__u8  reserved5[8];
} __packed;

/* IPL Parameter Block 0 for ECKD */
struct ipl_pb0_eckd {
	__u32 len;
	__u8  pbt;
	__u8  reserved1[3];
	__u32 reserved2[78];
	__u8  opt;
	__u8  reserved4[4];
	__u8  reserved5:5;
	__u8  ssid:3;
	__u16 devno;
	__u32 reserved6[5];
	__u32 bootprog;
	__u8  reserved7[12];
	struct {
		__u16 cyl;
		__u8 head;
		__u8 record;
		__u32 reserved;
	} br_chr __packed;
	__u32 scp_data_len;
	__u8  reserved8[260];
	__u8  scp_data[];
} __packed;

#define IPL_PB0_ECKD_OPT_IPL	0x10
#define IPL_PB0_ECKD_OPT_DUMP	0x20

#define IPL_PB0_CCW_VM_FLAG_NSS		0x80
#define IPL_PB0_CCW_VM_FLAG_VP		0x40

/* IPL Parameter Block 1 for additional SCP data */
struct ipl_pb1_scp_data {
	__u32 len;
	__u8  pbt;
	__u8  scp_data[];
} __packed;

/* IPL Report List header */
struct ipl_rl_hdr {
	__u32 len;
	__u8  flags;
	__u8  reserved1[2];
	__u8  version;
	__u8  reserved2[8];
} __packed;

/* IPL Report Block header */
struct ipl_rb_hdr {
	__u32 len;
	__u8  rbt;
	__u8  reserved1[11];
} __packed;

/* IPL Report Block types */
enum ipl_rbt {
	IPL_RBT_CERTIFICATES = 1,
	IPL_RBT_COMPONENTS = 2,
};

/* IPL Report Block for the certificate list */
struct ipl_rb_certificate_entry {
	__u64 addr;
	__u64 len;
} __packed;

struct ipl_rb_certificates {
	__u32 len;
	__u8  rbt;
	__u8  reserved1[11];
	struct ipl_rb_certificate_entry entries[];
} __packed;

/* IPL Report Block for the component list */
struct ipl_rb_component_entry {
	__u64 addr;
	__u64 len;
	__u8  flags;
	__u8  reserved1[5];
	__u16 certificate_index;
	__u8  reserved2[8];
};

#define IPL_RB_COMPONENT_FLAG_SIGNED	0x80
#define IPL_RB_COMPONENT_FLAG_VERIFIED	0x40

struct ipl_rb_components {
	__u32 len;
	__u8  rbt;
	__u8  reserved1[11];
	struct ipl_rb_component_entry entries[];
} __packed;

#endif
