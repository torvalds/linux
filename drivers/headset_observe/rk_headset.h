#ifndef RK_HEADSET_H
#define RK_HEADSET_H

#define HEADSET_IN_HIGH 0x00000001
#define HEADSET_IN_LOW  0x00000000

/* 耳机数据结构体 */
struct rk2818_headset_data {
	unsigned int gpio;
	unsigned int irq;
	unsigned int irq_type;
	unsigned int headset_in_type;
};

#endif
