#ifndef ADIS16255_H
#define ADIS16255_H

#include <linux/types.h>

struct adis16255_init_data {
	char direction;
	u8   negative;
	int  irq;
};

#endif
