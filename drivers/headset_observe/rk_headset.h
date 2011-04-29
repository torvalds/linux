#ifndef RK2818_HEADSET_H
#define RK2818_HEADSET_H

#define HEADSET_IN_HIGH 0x00000001
#define HEADSET_IN_LOW  0x00000000


struct rk2818_headset_data {
	unsigned int gpio;//Detection Headset--Must be set
	unsigned int irq;
	unsigned int irq_type;
	unsigned int headset_in_type;//	Headphones into the state level--Must be set
};

#endif
