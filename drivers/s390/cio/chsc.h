/* SPDX-License-Identifier: GPL-2.0 */
#ifndef S390_CHSC_H
#define S390_CHSC_H

#include <linux/types.h>
#include <linux/device.h>
#include <asm/css_chars.h>
#include <asm/chpid.h>
#include <asm/chsc.h>
#include <asm/schid.h>
#include <asm/qdio.h>

#define CHSC_SDA_OC_MSS   0x2

#define NR_MEASUREMENT_CHARS 5
struct cmg_chars {
	u32 values[NR_MEASUREMENT_CHARS];
};

struct cmg_cmcb {
	u32 not_valid : 1;
	u32 shared    : 1;
	u32 extended  : 1;
	u32	      : 21;
	u32 chpid     : 8;
	u32 cmcv      : 5;
	u32	      : 7;
	u32 cmgp      : 4;
	u32 cmgq      : 8;
	u32 cmg       : 8;
	u32	      : 16;
	u32 cmgs      : 16;
	u32 data[NR_MEASUREMENT_CHARS];
};

#define NR_MEASUREMENT_ENTRIES 8
struct cmg_entry {
	u32 values[NR_MEASUREMENT_ENTRIES];
};

#define NR_EXT_MEASUREMENT_ENTRIES 16
struct cmg_ext_entry {
	u32 values[NR_EXT_MEASUREMENT_ENTRIES];
};

struct channel_path_desc_fmt1 {
	u8 flags;
	u8 lsn;
	u8 desc;
	u8 chpid;
	u32:16;
	u8 esc;
	u8 chpp;
	u32 unused[2];
	u16 chid;
	u32:16;
	u16 mdc;
	u16:13;
	u8 r:1;
	u8 s:1;
	u8 f:1;
	u32 zeros[2];
};

struct channel_path_desc_fmt3 {
	struct channel_path_desc_fmt1 fmt1_desc;
	u8 util_str[64];
};

struct channel_path;

struct css_chsc_char {
	u64 res;
	u64 : 20;
	u32 secm : 1; /* bit 84 */
	u32 : 1;
	u32 scmc : 1; /* bit 86 */
	u32 : 20;
	u32 scssc : 1;  /* bit 107 */
	u32 scsscf : 1; /* bit 108 */
	u32:7;
	u32 pnso:1; /* bit 116 */
	u32:11;
} __packed;

extern struct css_chsc_char css_chsc_characteristics;

struct chsc_ssd_info {
	u8 path_mask;
	u8 fla_valid_mask;
	struct chp_id chpid[8];
	u16 fla[8];
};

struct chsc_ssqd_area {
	struct chsc_header request;
	u16:10;
	u8 ssid:2;
	u8 fmt:4;
	u16 first_sch;
	u16:16;
	u16 last_sch;
	u32:32;
	struct chsc_header response;
	u32:32;
	struct qdio_ssqd_desc qdio_ssqd;
} __packed __aligned(PAGE_SIZE);

struct chsc_scssc_area {
	struct chsc_header request;
	u16 operation_code;
	u16:16;
	u32:32;
	u32:32;
	dma64_t summary_indicator_addr;
	dma64_t subchannel_indicator_addr;
	u32 ks:4;
	u32 kc:4;
	u32:21;
	u32 isc:3;
	u32 word_with_d_bit;
	u32:32;
	struct subchannel_id schid;
	u32 reserved[1004];
	struct chsc_header response;
	u32:32;
} __packed __aligned(PAGE_SIZE);

struct chsc_scpd {
	struct chsc_header request;
	u32:2;
	u32 m:1;
	u32 c:1;
	u32 fmt:4;
	u32 cssid:8;
	u32:4;
	u32 rfmt:4;
	u32 first_chpid:8;
	u32:24;
	u32 last_chpid:8;
	u32 zeroes1;
	struct chsc_header response;
	u32:32;
	u8 data[];
} __packed __aligned(PAGE_SIZE);

struct chsc_sda_area {
	struct chsc_header request;
	u8 :4;
	u8 format:4;
	u8 :8;
	u16 operation_code;
	u32 :32;
	u32 :32;
	u32 operation_data_area[252];
	struct chsc_header response;
	u32 :4;
	u32 format2:4;
	u32 :24;
} __packed __aligned(PAGE_SIZE);

extern int chsc_get_ssd_info(struct subchannel_id schid,
			     struct chsc_ssd_info *ssd);
extern int chsc_determine_css_characteristics(void);
extern int chsc_init(void);
extern void chsc_init_cleanup(void);

int __chsc_enable_facility(struct chsc_sda_area *sda_area, int operation_code);
extern int chsc_enable_facility(int);
struct channel_subsystem;
extern int chsc_secm(struct channel_subsystem *, int);
int __chsc_do_secm(struct channel_subsystem *css, int enable);

int chsc_chp_vary(struct chp_id chpid, int on);
int chsc_determine_channel_path_desc(struct chp_id chpid, int fmt, int rfmt,
				     int c, int m, void *page);
int chsc_determine_fmt0_channel_path_desc(struct chp_id chpid,
					  struct channel_path_desc_fmt0 *desc);
int chsc_determine_fmt1_channel_path_desc(struct chp_id chpid,
					  struct channel_path_desc_fmt1 *desc);
int chsc_determine_fmt3_channel_path_desc(struct chp_id chpid,
					  struct channel_path_desc_fmt3 *desc);
void chsc_chp_online(struct chp_id chpid);
void chsc_chp_offline(struct chp_id chpid);
int chsc_get_channel_measurement_chars(struct channel_path *chp);
int chsc_ssqd(struct subchannel_id schid, struct chsc_ssqd_area *ssqd);
int chsc_sadc(struct subchannel_id schid, struct chsc_scssc_area *scssc,
	      dma64_t summary_indicator_addr, dma64_t subchannel_indicator_addr,
	      u8 isc);
int chsc_sgib(u32 origin);
int chsc_error_from_response(int response);

int chsc_siosl(struct subchannel_id schid);

/* Functions and definitions to query storage-class memory. */
struct sale {
	u64 sa;
	u32 p:4;
	u32 op_state:4;
	u32 data_state:4;
	u32 rank:4;
	u32 r:1;
	u32:7;
	u32 rid:8;
	u32:32;
} __packed;

struct chsc_scm_info {
	struct chsc_header request;
	u32:32;
	u64 reqtok;
	u32 reserved1[4];
	struct chsc_header response;
	u64:56;
	u8 rq;
	u32 mbc;
	u64 msa;
	u16 is;
	u16 mmc;
	u32 mci;
	u64 nr_scm_ini;
	u64 nr_scm_unini;
	u32 reserved2[10];
	u64 restok;
	struct sale scmal[248];
} __packed __aligned(PAGE_SIZE);

int chsc_scm_info(struct chsc_scm_info *scm_area, u64 token);

int chsc_pnso(struct subchannel_id schid, struct chsc_pnso_area *pnso_area,
	      u8 oc, struct chsc_pnso_resume_token resume_token, int cnc);

int __init chsc_get_cssid_iid(int idx, u8 *cssid, u8 *iid);

#ifdef CONFIG_SCM_BUS
int scm_update_information(void);
int scm_process_availability_information(void);
#else /* CONFIG_SCM_BUS */
static inline int scm_update_information(void) { return 0; }
static inline int scm_process_availability_information(void) { return 0; }
#endif /* CONFIG_SCM_BUS */


#endif
