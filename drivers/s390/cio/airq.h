#ifndef S390_AINTERRUPT_H
#define S390_AINTERRUPT_H

typedef	int (*adapter_int_handler_t)(void);

extern int s390_register_adapter_interrupt(adapter_int_handler_t handler);
extern int s390_unregister_adapter_interrupt(adapter_int_handler_t handler);
extern void do_adapter_IO (void);

#endif
