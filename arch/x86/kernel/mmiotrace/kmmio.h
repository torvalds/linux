#ifndef _LINUX_KMMIO_H
#define _LINUX_KMMIO_H

#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/version.h>
#include <linux/kdebug.h>

struct kmmio_probe;
struct kmmio_fault_page;
struct pt_regs;

typedef void (*kmmio_pre_handler_t)(struct kmmio_probe *,
				struct pt_regs *, unsigned long addr);
typedef void (*kmmio_post_handler_t)(struct kmmio_probe *,
				unsigned long condition, struct pt_regs *);

struct kmmio_probe {
	struct list_head list;

	/* start location of the probe point */
	unsigned long addr;

	/* length of the probe region */
	unsigned long len;

	 /* Called before addr is executed. */
	kmmio_pre_handler_t pre_handler;

	/* Called after addr is executed, unless... */
	kmmio_post_handler_t post_handler;
};

struct kmmio_fault_page {
	struct list_head list;

	/* location of the fault page */
	unsigned long page;

	int count;
};

/* kmmio is active by some kmmio_probes? */
static inline int is_kmmio_active(void)
{
	extern unsigned int kmmio_count;
	return kmmio_count;
}

int init_kmmio(void);
void cleanup_kmmio(void);
int register_kmmio_probe(struct kmmio_probe *p);
void unregister_kmmio_probe(struct kmmio_probe *p);

#endif /* _LINUX_KMMIO_H */
