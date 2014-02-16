#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

int fre(void *frD, void *frB)
{
#ifdef DEBUG
	printk("%s: %p %p\n", __func__, frD, frB);
#endif
	return -ENOSYS;
}
