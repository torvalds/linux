#ifndef _TPM_DEV_H
#define _TPM_DEV_H

#include "tpm.h"

struct file_priv {
	struct tpm_chip *chip;

	/* Data passed to and from the tpm via the read/write calls */
	atomic_t data_pending;
	struct mutex buffer_mutex;

	struct timer_list user_read_timer;      /* user needs to claim result */
	struct work_struct work;

	u8 data_buffer[TPM_BUFSIZE];
};

void tpm_common_open(struct file *file, struct tpm_chip *chip,
		     struct file_priv *priv);
ssize_t tpm_common_read(struct file *file, char __user *buf,
			size_t size, loff_t *off);
ssize_t tpm_common_write(struct file *file, const char __user *buf,
			 size_t size, loff_t *off, struct tpm_space *space);
void tpm_common_release(struct file *file, struct file_priv *priv);

#endif
