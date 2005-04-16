#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

int
fres(void *frD, void *frB)
{
#ifdef DEBUG
	printk("%s: %p %p\n", __FUNCTION__, frD, frB);
#endif
	return -ENOSYS;
}
