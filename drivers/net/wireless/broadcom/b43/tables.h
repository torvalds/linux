/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_TABLES_H_
#define B43_TABLES_H_

#define B43_TAB_ROTOR_SIZE	53
extern const u32 b43_tab_rotor[];
#define B43_TAB_RETARD_SIZE	53
extern const u32 b43_tab_retard[];
#define B43_TAB_FINEFREQA_SIZE	256
extern const u16 b43_tab_finefreqa[];
#define B43_TAB_FINEFREQG_SIZE	256
extern const u16 b43_tab_finefreqg[];
#define B43_TAB_NOISEA2_SIZE	8
extern const u16 b43_tab_noisea2[];
#define B43_TAB_NOISEA3_SIZE	8
extern const u16 b43_tab_noisea3[];
#define B43_TAB_NOISEG1_SIZE	8
extern const u16 b43_tab_noiseg1[];
#define B43_TAB_NOISEG2_SIZE	8
extern const u16 b43_tab_noiseg2[];
#define B43_TAB_NOISESCALE_SIZE	27
extern const u16 b43_tab_noisescalea2[];
extern const u16 b43_tab_noisescalea3[];
extern const u16 b43_tab_noisescaleg1[];
extern const u16 b43_tab_noisescaleg2[];
extern const u16 b43_tab_noisescaleg3[];
#define B43_TAB_SIGMASQR_SIZE	53
extern const u16 b43_tab_sigmasqr1[];
extern const u16 b43_tab_sigmasqr2[];
#define B43_TAB_RSSIAGC1_SIZE	16
extern const u16 b43_tab_rssiagc1[];
#define B43_TAB_RSSIAGC2_SIZE	48
extern const u16 b43_tab_rssiagc2[];

#endif /* B43_TABLES_H_ */
