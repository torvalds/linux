#ifndef CHIP_H
#define CHIP_H

#if defined(CONFIG_TOUCHSCREEN_CT36X_CHIP_CT360)
	#include "ct360.h"
#elif defined(CONFIG_TOUCHSCREEN_CT36X_CHIP_CT365)
	#include "ct365.h"
#else
#error XXXXXXXXXXXXXXXXXXXXXXXX
#endif

#endif

