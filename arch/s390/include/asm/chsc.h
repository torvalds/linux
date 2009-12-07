/*
 * ioctl interface for /dev/chsc
 *
 * Copyright 2008 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 */

#ifndef _ASM_CHSC_H
#define _ASM_CHSC_H

#include <linux/types.h>
#include <asm/chpid.h>
#include <asm/schid.h>

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
	__u8 data[PAGE_SIZE - 16 /* size of chsc_async_header */];
} __attribute__ ((packed));


struct chsc_response_struct {
	__u16 length;
	__u16 code;
	__u32 parms;
	__u8 data[PAGE_SIZE - 8];
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

#ifdef __KERNEL__

struct css_general_char {
	u64 : 12;
	u32 dynio : 1;	 /* bit 12 */
	u32 : 28;
	u32 aif : 1;	 /* bit 41 */
	u32 : 3;
	u32 mcss : 1;	 /* bit 45 */
	u32 fcs : 1;	 /* bit 46 */
	u32 : 1;
	u32 ext_mb : 1;  /* bit 48 */
	u32 : 7;
	u32 aif_tdd : 1; /* bit 56 */
	u32 : 1;
	u32 qebsm : 1;	 /* bit 58 */
	u32 : 8;
	u32 aif_osa : 1; /* bit 67 */
	u32 : 14;
	u32 cib : 1;	 /* bit 82 */
	u32 : 5;
	u32 fcx : 1;	 /* bit 88 */
	u32 : 7;
}__attribute__((packed));

extern struct css_general_char css_general_characteristics;

#endif /* __KERNEL__ */
#endif
