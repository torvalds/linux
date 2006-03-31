#ifndef __INCLUDED_TDA9840__
#define __INCLUDED_TDA9840__

#define	I2C_ADDR_TDA9840		0x42

#define TDA9840_DETECT		_IOR('v',1,int)
/* return values for TDA9840_DETCT */
#define TDA9840_MONO_DETECT		0x0
#define	TDA9840_DUAL_DETECT		0x1
#define	TDA9840_STEREO_DETECT		0x2
#define	TDA9840_INCORRECT_DETECT	0x3

#define TDA9840_SWITCH		_IOW('v',2,int)
/* modes than can be set with TDA9840_SWITCH */
#define	TDA9840_SET_MUTE		0x00
#define	TDA9840_SET_MONO		0x10
#define	TDA9840_SET_STEREO		0x2a
#define	TDA9840_SET_LANG1		0x12
#define	TDA9840_SET_LANG2		0x1e
#define	TDA9840_SET_BOTH		0x1a
#define	TDA9840_SET_BOTH_R		0x16
#define	TDA9840_SET_EXTERNAL		0x7a

/* values may range between +2.5 and -2.0;
   the value has to be multiplied with 10 */
#define TDA9840_LEVEL_ADJUST	_IOW('v',3,int)

/* values may range between +2.5 and -2.4;
   the value has to be multiplied with 10 */
#define TDA9840_STEREO_ADJUST	_IOW('v',4,int)

/* currently not implemented */
#define TDA9840_TEST		_IOW('v',5,int)

#endif
