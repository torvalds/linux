#ifndef S390_CHSC_H
#define S390_CHSC_H

#define NR_CHPIDS 256

#define CHSC_SEI_ACC_CHPID        1
#define CHSC_SEI_ACC_LINKADDR     2
#define CHSC_SEI_ACC_FULLLINKADDR 3

struct chsc_header {
	u16 length;
	u16 code;
};

struct channel_path_desc {
	u8 flags;
	u8 lsn;
	u8 desc;
	u8 chpid;
	u8 swla;
	u8 zeroes;
	u8 chla;
	u8 chpp;
};

struct channel_path {
	int id;
	int state;
	struct channel_path_desc desc;
	struct device dev;
};

extern void s390_process_css( void );
extern void chsc_validate_chpids(struct subchannel *);
extern void chpid_is_actually_online(int);

struct css_general_char {
	u64 : 41;
	u32 aif : 1;     /* bit 41 */
	u32 : 3;
	u32 mcss : 1;    /* bit 45 */
	u32 : 2;
	u32 ext_mb : 1;  /* bit 48 */
	u32 : 7;
	u32 aif_tdd : 1; /* bit 56 */
	u32 : 10;
	u32 aif_osa : 1; /* bit 67 */
	u32 : 28;
}__attribute__((packed));

struct css_chsc_char {
	u64 res;
	u64 : 43;
	u32 scssc : 1;  /* bit 107 */
	u32 scsscf : 1; /* bit 108 */
	u32 : 19;
}__attribute__((packed));

extern struct css_general_char css_general_characteristics;
extern struct css_chsc_char css_chsc_characteristics;

extern int chsc_determine_css_characteristics(void);
extern int css_characteristics_avail;

extern void *chsc_get_chp_desc(struct subchannel*, int);
#endif
