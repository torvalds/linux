/*
 * ioctl interface for /dev/chsc
 *
 * Copyright IBM Corp. 2008, 2012
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 */

#ifndef _ASM_CHSC_H
#define _ASM_CHSC_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/chpid.h>
#include <asm/schid.h>

#define CHSC_SIZE 0x1000

struct chsc_async_header {
	__u16 length;
	__u16 code;
	__u32 cmd_dependend;
	__u32 key : 4;
	__u32 : 28;
	struct subchannel_id sid;
} __attribute__ ((packed));

struct chsc_async_area {
	struct chsc_async_header header;
	__u8 data[CHSC_SIZE - sizeof(struct chsc_async_header)];
} __attribute__ ((packed));

struct chsc_header {
	__u16 length;
	__u16 code;
} __attribute__ ((packed));

struct chsc_sync_area {
	struct chsc_header header;
	__u8 data[CHSC_SIZE - sizeof(struct chsc_header)];
} __attribute__ ((packed));

struct chsc_response_struct {
	__u16 length;
	__u16 code;
	__u32 parms;
	__u8 data[CHSC_SIZE - 2 * sizeof(__u16) - sizeof(__u32)];
} __attribute__ ((packed));

struct chsc_chp_cd {
	struct chp_id chpid;
	int m;
	int fmt;
	struct chsc_response_struct cpcb;
};

struct chsc_cu_cd {
	__u16 cun;
	__u8 cssid;
	int m;
	int fmt;
	struct chsc_response_struct cucb;
};

struct chsc_sch_cud {
	struct subchannel_id schid;
	int fmt;
	struct chsc_response_struct scub;
};

struct conf_id {
	int m;
	__u8 cssid;
	__u8 ssid;
};

struct chsc_conf_info {
	struct conf_id id;
	int fmt;
	struct chsc_response_struct scid;
};

struct ccl_parm_chpid {
	int m;
	struct chp_id chp;
};

struct ccl_parm_cssids {
	__u8 f_cssid;
	__u8 l_cssid;
};

struct chsc_comp_list {
	struct {
		enum {
			CCL_CU_ON_CHP = 1,
			CCL_CHP_TYPE_CAP = 2,
			CCL_CSS_IMG = 4,
			CCL_CSS_IMG_CONF_CHAR = 5,
			CCL_IOP_CHP = 6,
		} ctype;
		int fmt;
		struct ccl_parm_chpid chpid;
		struct ccl_parm_cssids cssids;
	} req;
	struct chsc_response_struct sccl;
};

struct chsc_dcal {
	struct {
		enum {
			DCAL_CSS_IID_PN = 4,
		} atype;
		__u32 list_parm[2];
		int fmt;
	} req;
	struct chsc_response_struct sdcal;
};

struct chsc_cpd_info {
	struct chp_id chpid;
	int m;
	int fmt;
	int rfmt;
	int c;
	struct chsc_response_struct chpdb;
};

#define CHSC_IOCTL_MAGIC 'c'

#define CHSC_START _IOWR(CHSC_IOCTL_MAGIC, 0x81, struct chsc_async_area)
#define CHSC_INFO_CHANNEL_PATH _IOWR(CHSC_IOCTL_MAGIC, 0x82, \
				    struct chsc_chp_cd)
#define CHSC_INFO_CU _IOWR(CHSC_IOCTL_MAGIC, 0x83, struct chsc_cu_cd)
#define CHSC_INFO_SCH_CU _IOWR(CHSC_IOCTL_MAGIC, 0x84, struct chsc_sch_cud)
#define CHSC_INFO_CI _IOWR(CHSC_IOCTL_MAGIC, 0x85, struct chsc_conf_info)
#define CHSC_INFO_CCL _IOWR(CHSC_IOCTL_MAGIC, 0x86, struct chsc_comp_list)
#define CHSC_INFO_CPD _IOWR(CHSC_IOCTL_MAGIC, 0x87, struct chsc_cpd_info)
#define CHSC_INFO_DCAL _IOWR(CHSC_IOCTL_MAGIC, 0x88, struct chsc_dcal)
#define CHSC_START_SYNC _IOWR(CHSC_IOCTL_MAGIC, 0x89, struct chsc_sync_area)
#define CHSC_ON_CLOSE_SET _IOWR(CHSC_IOCTL_MAGIC, 0x8a, struct chsc_async_area)
#define CHSC_ON_CLOSE_REMOVE _IO(CHSC_IOCTL_MAGIC, 0x8b)

#endif
