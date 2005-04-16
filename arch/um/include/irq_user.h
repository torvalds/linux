/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __IRQ_USER_H__
#define __IRQ_USER_H__

enum { IRQ_READ, IRQ_WRITE };

extern void sigio_handler(int sig, union uml_pt_regs *regs);
extern int activate_fd(int irq, int fd, int type, void *dev_id);
extern void free_irq_by_irq_and_dev(unsigned int irq, void *dev_id);
extern void free_irq_by_fd(int fd);
extern void reactivate_fd(int fd, int irqnum);
extern void deactivate_fd(int fd, int irqnum);
extern int deactivate_all_fds(void);
extern void forward_interrupts(int pid);
extern void init_irq_signals(int on_sigstack);
extern void forward_ipi(int fd, int pid);
extern void free_irq_later(int irq, void *dev_id);
extern int activate_ipi(int fd, int pid);
extern unsigned long irq_lock(void);
extern void irq_unlock(unsigned long flags);
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
