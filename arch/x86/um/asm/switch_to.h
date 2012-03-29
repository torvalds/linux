#ifndef _ASM_UM_SWITCH_TO_H_
#define _ASM_UM_SWITCH_TO_H_

extern void *_switch_to(void *prev, void *next, void *last);
#define switch_to(prev, next, last) prev = _switch_to(prev, next, last)

#endif
