#ifndef __INCLUDED_TDA9840__
#define __INCLUDED_TDA9840__

#define	I2C_ADDR_TDA9840		0x42

/* values may range between +2.5 and -2.0;
   the value has to be multiplied with 10 */
#define TDA9840_LEVEL_ADJUST	_IOW('v',3,int)

/* values may range between +2.5 and -2.4;
   the value has to be multiplied with 10 */
#define TDA9840_STEREO_ADJUST	_IOW('v',4,int)

#endif
