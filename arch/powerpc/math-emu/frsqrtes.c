#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

int frsqrtes(void *frD, void *frB)
{
#ifdef DEBUG
	printk("%s: %p %p\n", __func__, frD, frB);
#endif
	return 0;
}
