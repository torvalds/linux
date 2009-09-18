#ifndef __ASM_KEYSC_H__
#define __ASM_KEYSC_H__

#define SH_KEYSC_MAXKEYS 30

struct sh_keysc_info {
	enum { SH_KEYSC_MODE_1, SH_KEYSC_MODE_2, SH_KEYSC_MODE_3 } mode;
	int scan_timing; /* 0 -> 7, see KYCR1, SCN[2:0] */
	int delay;
	int kycr2_delay;
	int keycodes[SH_KEYSC_MAXKEYS];
};

#endif /* __ASM_KEYSC_H__ */
