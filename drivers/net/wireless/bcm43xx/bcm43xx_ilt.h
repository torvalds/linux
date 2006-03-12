#ifndef BCM43xx_ILT_H_
#define BCM43xx_ILT_H_

#define BCM43xx_ILT_ROTOR_SIZE		53
extern const u32 bcm43xx_ilt_rotor[BCM43xx_ILT_ROTOR_SIZE];
#define BCM43xx_ILT_RETARD_SIZE		53
extern const u32 bcm43xx_ilt_retard[BCM43xx_ILT_RETARD_SIZE];
#define BCM43xx_ILT_FINEFREQA_SIZE	256
extern const u16 bcm43xx_ilt_finefreqa[BCM43xx_ILT_FINEFREQA_SIZE];
#define BCM43xx_ILT_FINEFREQG_SIZE	256
extern const u16 bcm43xx_ilt_finefreqg[BCM43xx_ILT_FINEFREQG_SIZE];
#define BCM43xx_ILT_NOISEA2_SIZE	8
extern const u16 bcm43xx_ilt_noisea2[BCM43xx_ILT_NOISEA2_SIZE];
#define BCM43xx_ILT_NOISEA3_SIZE	8
extern const u16 bcm43xx_ilt_noisea3[BCM43xx_ILT_NOISEA3_SIZE];
#define BCM43xx_ILT_NOISEG1_SIZE	8
extern const u16 bcm43xx_ilt_noiseg1[BCM43xx_ILT_NOISEG1_SIZE];
#define BCM43xx_ILT_NOISEG2_SIZE	8
extern const u16 bcm43xx_ilt_noiseg2[BCM43xx_ILT_NOISEG2_SIZE];
#define BCM43xx_ILT_NOISESCALEG_SIZE	27
extern const u16 bcm43xx_ilt_noisescaleg1[BCM43xx_ILT_NOISESCALEG_SIZE];
extern const u16 bcm43xx_ilt_noisescaleg2[BCM43xx_ILT_NOISESCALEG_SIZE];
extern const u16 bcm43xx_ilt_noisescaleg3[BCM43xx_ILT_NOISESCALEG_SIZE];
#define BCM43xx_ILT_SIGMASQR_SIZE	53
extern const u16 bcm43xx_ilt_sigmasqr1[BCM43xx_ILT_SIGMASQR_SIZE];
extern const u16 bcm43xx_ilt_sigmasqr2[BCM43xx_ILT_SIGMASQR_SIZE];


void bcm43xx_ilt_write(struct bcm43xx_private *bcm, u16 offset, u16 val);
u16 bcm43xx_ilt_read(struct bcm43xx_private *bcm, u16 offset);

#endif /* BCM43xx_ILT_H_ */
