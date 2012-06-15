#ifndef __ALPHA_SWITCH_TO_H
#define __ALPHA_SWITCH_TO_H


struct task_struct;
extern struct task_struct *alpha_switch_to(unsigned long, struct task_struct *);

#define switch_to(P,N,L)						 \
  do {									 \
    (L) = alpha_switch_to(virt_to_phys(&task_thread_info(N)->pcb), (P)); \
    check_mmu_context();						 \
  } while (0)

#endif /* __ALPHA_SWITCH_TO_H */
