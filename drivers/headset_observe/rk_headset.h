#ifndef RK_HEADSET_H
#define RK_HEADSET_H

#define HEADSET_IN_HIGH 0x00000001
#define HEADSET_IN_LOW  0x00000000


struct rk_headset_pdata{
	unsigned int Hook_gpio;//Detection Headset--Must be set
	int	hook_key_code;
	unsigned int Headset_gpio;//Detection Headset--Must be set
	unsigned int headset_in_type;//	Headphones into the state level--Must be set	
};

#endif
