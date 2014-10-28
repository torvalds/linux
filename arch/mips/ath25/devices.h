#ifndef __ATH25_DEVICES_H
#define __ATH25_DEVICES_H

#include <linux/cpu.h>

#define ATH25_REG_MS(_val, _field)	(((_val) & _field##_M) >> _field##_S)

static inline bool is_ar2315(void)
{
	return (current_cpu_data.cputype == CPU_4KEC);
}

static inline bool is_ar5312(void)
{
	return !is_ar2315();
}

#endif
