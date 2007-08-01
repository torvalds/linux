#ifndef S390_CHSC_H
#define S390_CHSC_H

#include <linux/types.h>
#include <linux/device.h>
#include <asm/chpid.h>
#include "schid.h"

#define CHSC_SDA_OC_MSS   0x2

struct chsc_header {
	u16 length;
	u16 code;
} __attribute__ ((packed));

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

struct channel_path;

extern void chsc_process_crw(void);

struct css_general_char {
	u64 : 41;
	u32 aif : 1;     /* bit 41 */
	u32 : 3;
	u32 mcss : 1;    /* bit 45 */
	u32 : 2;
	u32 ext_mb : 1;  /* bit 48 */
	u32 : 7;
	u32 aif_tdd : 1; /* bit 56 */
	u32 : 1;
	u32 qebsm : 1;   /* bit 58 */
	u32 : 8;
	u32 aif_osa : 1; /* bit 67 */
	u32 : 28;
}__attribute__((packed));

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

extern struct css_general_char css_general_characteristics;
extern struct css_chsc_char css_chsc_characteristics;

struct chsc_ssd_info {
	u8 path_mask;
	u8 fla_valid_mask;
	struct chp_id chpid[8];
	u16 fla[8];
};
extern int chsc_get_ssd_info(struct subchannel_id schid,
			     struct chsc_ssd_info *ssd);
extern int chsc_determine_css_characteristics(void);
extern int css_characteristics_avail;
extern int chsc_alloc_sei_area(void);
extern void chsc_free_sei_area(void);

extern int chsc_enable_facility(int);
struct channel_subsystem;
extern int chsc_secm(struct channel_subsystem *, int);

int chsc_chp_vary(struct chp_id chpid, int on);
int chsc_determine_channel_path_description(struct chp_id chpid,
					    struct channel_path_desc *desc);
void chsc_chp_online(struct chp_id chpid);
void chsc_chp_offline(struct chp_id chpid);
int chsc_get_channel_measurement_chars(struct channel_path *chp);

#endif
