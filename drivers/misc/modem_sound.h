
#ifndef __MODEM_SOUND_H__
#define __MODEM_SOUND_H__

struct modem_sound_data {
	int spkctl_io;
	int spkctl_active;
	int codec_flag;
	struct semaphore power_sem;
	struct workqueue_struct *wq;
	struct work_struct work;
};

#endif
