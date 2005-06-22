/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/time.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "process.h"
#include "signal_user.h"
#include "sigio.h"
#include "irq_user.h"
#include "os.h"

struct irq_fd {
	struct irq_fd *next;
	void *id;
	int fd;
	int type;
	int irq;
	int pid;
	int events;
	int current_events;
	int freed;
};

static struct irq_fd *active_fds = NULL;
static struct irq_fd **last_irq_ptr = &active_fds;

static struct pollfd *pollfds = NULL;
static int pollfds_num = 0;
static int pollfds_size = 0;

extern int io_count, intr_count;

void sigio_handler(int sig, union uml_pt_regs *regs)
{
	struct irq_fd *irq_fd, *next;
	int i, n;

	if(smp_sigio_handler()) return;
	while(1){
		n = poll(pollfds, pollfds_num, 0);
		if(n < 0){
			if(errno == EINTR) continue;
			printk("sigio_handler : poll returned %d, "
			       "errno = %d\n", n, errno);
			break;
		}
		if(n == 0) break;

		irq_fd = active_fds;
		for(i = 0; i < pollfds_num; i++){
			if(pollfds[i].revents != 0){
				irq_fd->current_events = pollfds[i].revents;
				pollfds[i].fd = -1;
			}
			irq_fd = irq_fd->next;
		}

		for(irq_fd = active_fds; irq_fd != NULL; irq_fd = next){
			next = irq_fd->next;
			if(irq_fd->current_events != 0){
				irq_fd->current_events = 0;
				do_IRQ(irq_fd->irq, regs);

				/* This is here because the next irq may be
				 * freed in the handler.  If a console goes
				 * away, both the read and write irqs will be
				 * freed.  After do_IRQ, ->next will point to
				 * a good IRQ.
				 * Irqs can't be freed inside their handlers,
				 * so the next best thing is to have them
				 * marked as needing freeing, so that they
				 * can be freed here.
				 */
				next = irq_fd->next;
				if(irq_fd->freed){
					free_irq(irq_fd->irq, irq_fd->id);
				}
			}
		}
	}
}

int activate_ipi(int fd, int pid)
{
	return(os_set_fd_async(fd, pid));
}

static void maybe_sigio_broken(int fd, int type)
{
	if(isatty(fd)){
		if((type == IRQ_WRITE) && !pty_output_sigio){
			write_sigio_workaround();
			add_sigio_fd(fd, 0);
		}
		else if((type == IRQ_READ) && !pty_close_sigio){
			write_sigio_workaround();
			add_sigio_fd(fd, 1);			
		}
	}
}

int activate_fd(int irq, int fd, int type, void *dev_id)
{
	struct pollfd *tmp_pfd;
	struct irq_fd *new_fd, *irq_fd;
	unsigned long flags;
	int pid, events, err, n, size;

	pid = os_getpid();
	err = os_set_fd_async(fd, pid);
	if(err < 0)
		goto out;

	new_fd = um_kmalloc(sizeof(*new_fd));
	err = -ENOMEM;
	if(new_fd == NULL)
		goto out;

	if(type == IRQ_READ) events = POLLIN | POLLPRI;
	else events = POLLOUT;
	*new_fd = ((struct irq_fd) { .next  		= NULL,
				     .id 		= dev_id,
				     .fd 		= fd,
				     .type 		= type,
				     .irq 		= irq,
				     .pid  		= pid,
				     .events 		= events,
				     .current_events 	= 0,
				     .freed 		= 0  } );

	/* Critical section - locked by a spinlock because this stuff can
	 * be changed from interrupt handlers.  The stuff above is done 
	 * outside the lock because it allocates memory.
	 */

	/* Actually, it only looks like it can be called from interrupt
	 * context.  The culprit is reactivate_fd, which calls 
	 * maybe_sigio_broken, which calls write_sigio_workaround,
	 * which calls activate_fd.  However, write_sigio_workaround should
	 * only be called once, at boot time.  That would make it clear that
	 * this is called only from process context, and can be locked with
	 * a semaphore.
	 */
	flags = irq_lock();
	for(irq_fd = active_fds; irq_fd != NULL; irq_fd = irq_fd->next){
		if((irq_fd->fd == fd) && (irq_fd->type == type)){
			printk("Registering fd %d twice\n", fd);
			printk("Irqs : %d, %d\n", irq_fd->irq, irq);
			printk("Ids : 0x%x, 0x%x\n", irq_fd->id, dev_id);
			goto out_unlock;
		}
	}

	n = pollfds_num;
	if(n == pollfds_size){
		while(1){
			/* Here we have to drop the lock in order to call 
			 * kmalloc, which might sleep.  If something else
			 * came in and changed the pollfds array, we free
			 * the buffer and try again.
			 */
			irq_unlock(flags);
			size = (pollfds_num + 1) * sizeof(pollfds[0]);
			tmp_pfd = um_kmalloc(size);
			flags = irq_lock();
			if(tmp_pfd == NULL)
				goto out_unlock;
			if(n == pollfds_size)
				break;
			kfree(tmp_pfd);
		}
		if(pollfds != NULL){
			memcpy(tmp_pfd, pollfds,
			       sizeof(pollfds[0]) * pollfds_size);
			kfree(pollfds);
		}
		pollfds = tmp_pfd;
		pollfds_size++;
	}

	if(type == IRQ_WRITE) 
		fd = -1;

	pollfds[pollfds_num] = ((struct pollfd) { .fd 	= fd,
						  .events 	= events,
						  .revents 	= 0 });
	pollfds_num++;

	*last_irq_ptr = new_fd;
	last_irq_ptr = &new_fd->next;

	irq_unlock(flags);

	/* This calls activate_fd, so it has to be outside the critical
	 * section.
	 */
	maybe_sigio_broken(fd, type);

	return(0);

 out_unlock:
	irq_unlock(flags);
	kfree(new_fd);
 out:
	return(err);
}

static void free_irq_by_cb(int (*test)(struct irq_fd *, void *), void *arg)
{
	struct irq_fd **prev;
	unsigned long flags;
	int i = 0;

	flags = irq_lock();
	prev = &active_fds;
	while(*prev != NULL){
		if((*test)(*prev, arg)){
			struct irq_fd *old_fd = *prev;
			if((pollfds[i].fd != -1) && 
			   (pollfds[i].fd != (*prev)->fd)){
				printk("free_irq_by_cb - mismatch between "
				       "active_fds and pollfds, fd %d vs %d\n",
				       (*prev)->fd, pollfds[i].fd);
				goto out;
			}

			pollfds_num--;

			/* This moves the *whole* array after pollfds[i] (though
			 * it doesn't spot as such)! */

			memmove(&pollfds[i], &pollfds[i + 1],
			       (pollfds_num - i) * sizeof(pollfds[0]));

			if(last_irq_ptr == &old_fd->next) 
				last_irq_ptr = prev;
			*prev = (*prev)->next;
			if(old_fd->type == IRQ_WRITE) 
				ignore_sigio_fd(old_fd->fd);
			kfree(old_fd);
			continue;
		}
		prev = &(*prev)->next;
		i++;
	}
 out:
	irq_unlock(flags);
}

struct irq_and_dev {
	int irq;
	void *dev;
};

static int same_irq_and_dev(struct irq_fd *irq, void *d)
{
	struct irq_and_dev *data = d;

	return((irq->irq == data->irq) && (irq->id == data->dev));
}

void free_irq_by_irq_and_dev(unsigned int irq, void *dev)
{
	struct irq_and_dev data = ((struct irq_and_dev) { .irq  = irq,
							  .dev  = dev });

	free_irq_by_cb(same_irq_and_dev, &data);
}

static int same_fd(struct irq_fd *irq, void *fd)
{
	return(irq->fd == *((int *) fd));
}

void free_irq_by_fd(int fd)
{
	free_irq_by_cb(same_fd, &fd);
}

static struct irq_fd *find_irq_by_fd(int fd, int irqnum, int *index_out)
{
	struct irq_fd *irq;
	int i = 0;

	for(irq=active_fds; irq != NULL; irq = irq->next){
		if((irq->fd == fd) && (irq->irq == irqnum)) break;
		i++;
	}
	if(irq == NULL){
		printk("find_irq_by_fd doesn't have descriptor %d\n", fd);
		goto out;
	}
	if((pollfds[i].fd != -1) && (pollfds[i].fd != fd)){
		printk("find_irq_by_fd - mismatch between active_fds and "
		       "pollfds, fd %d vs %d, need %d\n", irq->fd, 
		       pollfds[i].fd, fd);
		irq = NULL;
		goto out;
	}
	*index_out = i;
 out:
	return(irq);
}

void free_irq_later(int irq, void *dev_id)
{
	struct irq_fd *irq_fd;
	unsigned long flags;

	flags = irq_lock();
	for(irq_fd = active_fds; irq_fd != NULL; irq_fd = irq_fd->next){
		if((irq_fd->irq == irq) && (irq_fd->id == dev_id))
			break;
	}
	if(irq_fd == NULL){
		printk("free_irq_later found no irq, irq = %d, "
		       "dev_id = 0x%p\n", irq, dev_id);
		goto out;
	}
	irq_fd->freed = 1;
 out:
	irq_unlock(flags);
}

void reactivate_fd(int fd, int irqnum)
{
	struct irq_fd *irq;
	unsigned long flags;
	int i;

	flags = irq_lock();
	irq = find_irq_by_fd(fd, irqnum, &i);
	if(irq == NULL){
		irq_unlock(flags);
		return;
	}

	pollfds[i].fd = irq->fd;

	irq_unlock(flags);

	/* This calls activate_fd, so it has to be outside the critical
	 * section.
	 */
	maybe_sigio_broken(fd, irq->type);
}

void deactivate_fd(int fd, int irqnum)
{
	struct irq_fd *irq;
	unsigned long flags;
	int i;

	flags = irq_lock();
	irq = find_irq_by_fd(fd, irqnum, &i);
	if(irq == NULL)
		goto out;
	pollfds[i].fd = -1;
 out:
	irq_unlock(flags);
}

int deactivate_all_fds(void)
{
	struct irq_fd *irq;
	int err;

	for(irq=active_fds;irq != NULL;irq = irq->next){
		err = os_clear_fd_async(irq->fd);
		if(err)
			return(err);
	}
	/* If there is a signal already queued, after unblocking ignore it */
	set_handler(SIGIO, SIG_IGN, 0, -1);

	return(0);
}

void forward_ipi(int fd, int pid)
{
	int err;

	err = os_set_owner(fd, pid);
	if(err < 0)
		printk("forward_ipi: set_owner failed, fd = %d, me = %d, "
		       "target = %d, err = %d\n", fd, os_getpid(), pid, -err);
}

void forward_interrupts(int pid)
{
	struct irq_fd *irq;
	unsigned long flags;
	int err;

	flags = irq_lock();
	for(irq=active_fds;irq != NULL;irq = irq->next){
		err = os_set_owner(irq->fd, pid);
		if(err < 0){
			/* XXX Just remove the irq rather than
			 * print out an infinite stream of these
			 */
			printk("Failed to forward %d to pid %d, err = %d\n",
			       irq->fd, pid, -err);
		}

		irq->pid = pid;
	}
	irq_unlock(flags);
}

void init_irq_signals(int on_sigstack)
{
	__sighandler_t h;
	int flags;

	flags = on_sigstack ? SA_ONSTACK : 0;
	if(timer_irq_inited) h = (__sighandler_t) alarm_handler;
	else h = boot_timer_handler;

	set_handler(SIGVTALRM, h, flags | SA_RESTART, 
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, -1);
	set_handler(SIGIO, (__sighandler_t) sig_handler, flags | SA_RESTART,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	signal(SIGWINCH, SIG_IGN);
}

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
