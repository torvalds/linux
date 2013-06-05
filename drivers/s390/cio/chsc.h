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
} __attribute__ ((packed));

#define NR_MEASUREMENT_ENTRIES 8
struct cmg_entry {
	u32 values[NR_MEASUREMENT_ENTRIES];
} __attribute__ ((packed));

struct channel_path_desc {
	u8 flags;
	u8 lsn;
	u8 desc;
	u8 chpid;
	u8 swla;
	u8 zeroes;
	u8 chla;
	u8 chpp;
} __attribute__ ((packed));

struct channel_path_desc_fmt1 {
	u8 flags;
	u8 lsn;
	u8 desc;
	u8 chpid;
	u32:24;
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
} __attribute__ ((packed));

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
	u32 : 19;
}__attribute__((packed));

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
} __packed;

struct chsc_scssc_area {
	struct chsc_header request;
	u16 operation_code;
	u16:16;
	u32:32;
	u32:32;
	u64 summary_indicator_addr;
	u64 subchannel_indicator_addr;
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
} __packed;

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
	u8 data[PAGE_SIZE - 20];
} __attribute__ ((packed));


extern int chsc_get_ssd_info(struct subchannel_id schid,
			     struct chsc_ssd_info *ssd);
extern int chsc_determine_css_characteristics(void);
extern int chsc_init(void);
extern void chsc_init_cleanup(void);

extern int chsc_enable_facility(int);
struct channel_subsystem;
extern int chsc_secm(struct channel_subsystem *, int);
int __chsc_do_secm(struct channel_subsystem *css, int enable);

int chsc_chp_vary(struct chp_id chpid, int on);
int chsc_determine_channel_path_desc(struct chp_id chpid, int fmt, int rfmt,
				     int c, int m, void *page);
int chsc_determine_base_channel_path_desc(struct chp_id chpid,
					  struct channel_path_desc *desc);
int chsc_determine_fmt1_channel_path_desc(struct chp_id chpid,
					  struct channel_path_desc_fmt1 *desc);
void chsc_chp_online(struct chp_id chpid);
void chsc_chp_offline(struct chp_id chpid);
int chsc_get_channel_measurement_chars(struct channel_path *chp);
int chsc_ssqd(struct subchannel_id schid, struct chsc_ssqd_area *ssqd);
int chsc_sadc(struct subchannel_id schid, struct chsc_scssc_area *scssc,
	      u64 summary_indicator_addr, u64 subchannel_indicator_addr);
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
} __packed;

int chsc_scm_info(struct chsc_scm_info *scm_area, u64 token);

#ifdef CONFIG_SCM_BUS
int scm_update_information(void);
int scm_process_availability_information(void);
#else /* CONFIG_SCM_BUS */
static inline int scm_update_information(void) { return 0; }
static inline int scm_process_availability_information(void) { return 0; }
#endif /* CONFIG_SCM_BUS */


#endif
