#ifndef _MALI_CLOCK_H_
#define _MALI_CLOCK_H_

typedef int (*critical_t)(size_t param);
int mali_clock_critical(critical_t critical, size_t param);

int mali_clock_init(mali_plat_info_t*);
int mali_clock_set(unsigned int index);
void disable_clock(void);
void enable_clock(void);
u32 get_mali_freq(u32 idx);
void set_str_src(u32 data);
#endif /* _MALI_CLOCK_H_ */
