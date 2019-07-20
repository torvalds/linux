/* SPDX-License-Identifier: GPL-2.0 */
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
#include <uapi/asm/ipl.h>

struct ipl_parameter_block {
	struct ipl_pl_hdr hdr;
	union {
		struct ipl_pb_hdr pb0_hdr;
		struct ipl_pb0_common common;
		struct ipl_pb0_fcp fcp;
		struct ipl_pb0_ccw ccw;
		char raw[PAGE_SIZE - sizeof(struct ipl_pl_hdr)];
	};
} __packed __aligned(PAGE_SIZE);

#define NSS_NAME_SIZE 8

#define IPL_BP_FCP_LEN (sizeof(struct ipl_pl_hdr) + \
			      sizeof(struct ipl_pb0_fcp))
#define IPL_BP0_FCP_LEN (sizeof(struct ipl_pb0_fcp))
#define IPL_BP_CCW_LEN (sizeof(struct ipl_pl_hdr) + \
			      sizeof(struct ipl_pb0_ccw))
#define IPL_BP0_CCW_LEN (sizeof(struct ipl_pb0_ccw))

#define IPL_MAX_SUPPORTED_VERSION (0)

#define IPL_RB_CERT_UNKNOWN ((unsigned short)-1)

#define DIAG308_VMPARM_SIZE (64)
#define DIAG308_SCPDATA_OFFSET offsetof(struct ipl_parameter_block, \
					fcp.scp_data)
#define DIAG308_SCPDATA_SIZE (PAGE_SIZE - DIAG308_SCPDATA_OFFSET)

struct save_area;
struct save_area * __init save_area_alloc(bool is_boot_cpu);
struct save_area * __init save_area_boot_cpu(void);
void __init save_area_add_regs(struct save_area *, void *regs);
void __init save_area_add_vxrs(struct save_area *, __vector128 *vxrs);

extern void s390_reset_system(void);
extern size_t ipl_block_get_ascii_vmparm(char *dest, size_t size,
					 const struct ipl_parameter_block *ipb);

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
extern void set_os_info_reipl_block(void);

struct ipl_report {
	struct ipl_parameter_block *ipib;
	struct list_head components;
	struct list_head certificates;
	size_t size;
};

struct ipl_report_component {
	struct list_head list;
	struct ipl_rb_component_entry entry;
};

struct ipl_report_certificate {
	struct list_head list;
	struct ipl_rb_certificate_entry entry;
	void *key;
};

struct kexec_buf;
struct ipl_report *ipl_report_init(struct ipl_parameter_block *ipib);
void *ipl_report_finish(struct ipl_report *report);
int ipl_report_free(struct ipl_report *report);
int ipl_report_add_component(struct ipl_report *report, struct kexec_buf *kbuf,
			     unsigned char flags, unsigned short cert);
int ipl_report_add_certificate(struct ipl_report *report, void *key,
			       unsigned long addr, unsigned long len);

/*
 * DIAG 308 support
 */
enum diag308_subcode  {
	DIAG308_REL_HSA = 2,
	DIAG308_LOAD_CLEAR = 3,
	DIAG308_LOAD_NORMAL_DUMP = 4,
	DIAG308_SET = 5,
	DIAG308_STORE = 6,
};

enum diag308_rc {
	DIAG308_RC_OK		= 0x0001,
	DIAG308_RC_NOCONFIG	= 0x0102,
};

extern int diag308(unsigned long subcode, void *addr);
extern void store_status(void (*fn)(void *), void *data);
extern void lgr_info_log(void);

#endif /* _ASM_S390_IPL_H */
