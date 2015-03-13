#ifndef __BACKPORT_SEQ_FILE_H
#define __BACKPORT_SEQ_FILE_H
#include_next <linux/seq_file.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#include <linux/user_namespace.h>
#include <linux/file.h>
#include <linux/fs.h>
#ifdef CONFIG_USER_NS
static inline struct user_namespace *seq_user_ns(struct seq_file *seq)
{
	struct file *f = container_of((void *) seq, struct file, private_data);

	return f->f_cred->user_ns;
}
#else
static inline struct user_namespace *seq_user_ns(struct seq_file *seq)
{
	extern struct user_namespace init_user_ns;
	return &init_user_ns;
}
#endif /* CONFIG_USER_NS */
#endif /* < 3.7 */

#endif /* __BACKPORT_SEQ_FILE_H */
