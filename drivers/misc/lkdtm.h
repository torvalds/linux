#ifndef __LKDTM_H
#define __LKDTM_H

/* lkdtm_rodata.c */
void lkdtm_rodata_do_nothing(void);

/* lkdtm_usercopy.c */
void __init lkdtm_usercopy_init(void);
void __exit lkdtm_usercopy_exit(void);
void lkdtm_USERCOPY_HEAP_SIZE_TO(void);
void lkdtm_USERCOPY_HEAP_SIZE_FROM(void);
void lkdtm_USERCOPY_HEAP_FLAG_TO(void);
void lkdtm_USERCOPY_HEAP_FLAG_FROM(void);
void lkdtm_USERCOPY_STACK_FRAME_TO(void);
void lkdtm_USERCOPY_STACK_FRAME_FROM(void);
void lkdtm_USERCOPY_STACK_BEYOND(void);
void lkdtm_USERCOPY_KERNEL(void);

#endif
