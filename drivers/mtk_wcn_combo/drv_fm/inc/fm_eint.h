#ifndef __FM_EINT_H__
#define __FM_EINT_H__

#include "fm_typedef.h"

enum{
	FM_EINT_PIN_EINT_MODE,
	FM_EINT_PIN_GPIO_MODE,
	FM_EINT_PIN_MAX_MODE
};

extern fm_s32 fm_enable_eint(void);
extern fm_s32 fm_disable_eint(void);
extern fm_s32 fm_request_eint(void (*parser)(void));  
extern fm_s32 fm_eint_pin_cfg(fm_s32 mode);

#endif //__FM_EINT_H__

