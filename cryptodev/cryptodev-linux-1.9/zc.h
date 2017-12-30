#ifndef ZC_H
# define ZC_H

#include "cryptodev_int.h"

/* For zero copy */
int __get_userbuf(uint8_t __user *addr, uint32_t len, int write,
		unsigned int pgcount, struct page **pg, struct scatterlist *sg,
		struct task_struct *task, struct mm_struct *mm);
void release_user_pages(struct csession *ses);

int get_userbuf(struct csession *ses,
                void *__user src, unsigned int src_len,
                void *__user dst, unsigned int dst_len,
                struct task_struct *task, struct mm_struct *mm,
                struct scatterlist **src_sg,
                struct scatterlist **dst_sg);

/* buflen ? (last page - first page + 1) : 0 */
#define PAGECOUNT(buf, buflen) ((buflen) \
	? ((((unsigned long)(buf + buflen - 1)) >> PAGE_SHIFT) - \
	   (((unsigned long)(buf             )) >> PAGE_SHIFT) + 1) \
	: 0)

#define DEFAULT_PREALLOC_PAGES 32

#endif
