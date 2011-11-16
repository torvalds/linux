#ifndef __LINUX_HV_KEYPAD_H__
#define __LINUX_HV_KEYPAD_H__



#define HV_NAME	"hv_keypad"

struct hv_keypad_platform_data{
	u16	intr;		/* irq number	*/
};

#define PIO_BASE_ADDRESS (0xf1c20800)
#define PIOA_CFG1_REG    (PIO_BASE_ADDRESS+0x4)
#define PIOA_DATA        (PIO_BASE_ADDRESS+0x10)  
#define DELAY_PERIOD     (5)


#endif //__LINUX_HV_KEYPAD_H__

