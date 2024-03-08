/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43legacy_ILT_H_
#define B43legacy_ILT_H_

#define B43legacy_ILT_ROTOR_SIZE	53
extern const u32 b43legacy_ilt_rotor[B43legacy_ILT_ROTOR_SIZE];
#define B43legacy_ILT_RETARD_SIZE	53
extern const u32 b43legacy_ilt_retard[B43legacy_ILT_RETARD_SIZE];
#define B43legacy_ILT_FINEFREQA_SIZE	256
extern const u16 b43legacy_ilt_finefreqa[B43legacy_ILT_FINEFREQA_SIZE];
#define B43legacy_ILT_FINEFREQG_SIZE	256
extern const u16 b43legacy_ilt_finefreqg[B43legacy_ILT_FINEFREQG_SIZE];
#define B43legacy_ILT_ANALISEA2_SIZE	8
extern const u16 b43legacy_ilt_analisea2[B43legacy_ILT_ANALISEA2_SIZE];
#define B43legacy_ILT_ANALISEA3_SIZE	8
extern const u16 b43legacy_ilt_analisea3[B43legacy_ILT_ANALISEA3_SIZE];
#define B43legacy_ILT_ANALISEG1_SIZE	8
extern const u16 b43legacy_ilt_analiseg1[B43legacy_ILT_ANALISEG1_SIZE];
#define B43legacy_ILT_ANALISEG2_SIZE	8
extern const u16 b43legacy_ilt_analiseg2[B43legacy_ILT_ANALISEG2_SIZE];
#define B43legacy_ILT_ANALISESCALEG_SIZE	27
extern const u16 b43legacy_ilt_analisescaleg1[B43legacy_ILT_ANALISESCALEG_SIZE];
extern const u16 b43legacy_ilt_analisescaleg2[B43legacy_ILT_ANALISESCALEG_SIZE];
extern const u16 b43legacy_ilt_analisescaleg3[B43legacy_ILT_ANALISESCALEG_SIZE];
#define B43legacy_ILT_SIGMASQR_SIZE	53
extern const u16 b43legacy_ilt_sigmasqr1[B43legacy_ILT_SIGMASQR_SIZE];
extern const u16 b43legacy_ilt_sigmasqr2[B43legacy_ILT_SIGMASQR_SIZE];


void b43legacy_ilt_write(struct b43legacy_wldev *dev, u16 offset, u16 val);
void b43legacy_ilt_write32(struct b43legacy_wldev *dev, u16 offset,
			   u32 val);
u16 b43legacy_ilt_read(struct b43legacy_wldev *dev, u16 offset);

#endif /* B43legacy_ILT_H_ */
