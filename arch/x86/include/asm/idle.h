#ifndef ASM_X86__IDLE_H
#define ASM_X86__IDLE_H

#define IDLE_START 1
#define IDLE_END 2

struct notifier_block;
void idle_notifier_register(struct notifier_block *n);

void enter_idle(void);
void exit_idle(void);

void c1e_remove_cpu(int cpu);

#endif /* ASM_X86__IDLE_H */
