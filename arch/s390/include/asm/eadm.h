#ifndef _ASM_S390_EADM_H
#define _ASM_S390_EADM_H

#include <linux/types.h>

struct arqb {
	u64 data;
	u16 fmt:4;
	u16:12;
	u16 cmd_code;
	u16:16;
	u16 msb_count;
	u32 reserved[12];
} __packed;

#define ARQB_CMD_MOVE	1

struct arsb {
	u16 fmt:4;
	u32:28;
	u8 ef;
	u8:8;
	u8 ecbi;
	u8:8;
	u8 fvf;
	u16:16;
	u8 eqc;
	u32:32;
	u64 fail_msb;
	u64 fail_aidaw;
	u64 fail_ms;
	u64 fail_scm;
	u32 reserved[4];
} __packed;

struct msb {
	u8 fmt:4;
	u8 oc:4;
	u8 flags;
	u16:12;
	u16 bs:4;
	u32 blk_count;
	u64 data_addr;
	u64 scm_addr;
	u64:64;
} __packed;

struct aidaw {
	u8 flags;
	u32 :24;
	u32 :32;
	u64 data_addr;
} __packed;

#define MSB_OC_CLEAR	0
#define MSB_OC_READ	1
#define MSB_OC_WRITE	2
#define MSB_OC_RELEASE	3

#define MSB_FLAG_BNM	0x80
#define MSB_FLAG_IDA	0x40

#define MSB_BS_4K	0
#define MSB_BS_1M	1

#define AOB_NR_MSB	124

struct aob {
	struct arqb request;
	struct arsb response;
	struct msb msb[AOB_NR_MSB];
} __packed __aligned(PAGE_SIZE);

#endif /* _ASM_S390_EADM_H */
