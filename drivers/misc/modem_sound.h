
#ifndef __MODEM_SOUND_H__
#define __MODEM_SOUND_H__
#include <linux/ioctl.h>

#define MODEM_SOUND                   0x1B

#define IOCTL_MODEM_EAR_PHOEN      	        _IO(MODEM_SOUND, 0x01)
#define IOCTL_MODEM_SPK_PHONE      	        _IO(MODEM_SOUND, 0x02) 
#define IOCTL_MODEM_HP_PHONE      	        _IO(MODEM_SOUND, 0x03)
#define IOCTL_MODEM_BT_PHONE      	        _IO(MODEM_SOUND, 0x04)
#define IOCTL_MODEM_STOP_PHONE      	    _IO(MODEM_SOUND, 0x05) 

#define IOCTL_SET_EAR_VALUME      	    _IO(MODEM_SOUND, 0x11) 
#define IOCTL_SET_SPK_VALUME      	    _IO(MODEM_SOUND, 0x12) 
#define IOCTL_SET_HP_VALUME      	    _IO(MODEM_SOUND, 0x13) 
#define IOCTL_SET_BT_VALUME      	    _IO(MODEM_SOUND, 0x14) 
 

struct modem_sound_data {
	int spkctl_io;
	int spkctl_active;
	int codec_flag;
	struct semaphore power_sem;
	struct workqueue_struct *wq;
	struct work_struct work;
};

#endif
