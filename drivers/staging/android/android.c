#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

static int __init android_init(void)
{
	return 0;
}

static void __exit android_exit(void)
{
}

module_init(android_init);
module_exit(android_exit);

MODULE_AUTHOR("Greg Kroah-Hartman");
MODULE_LICENSE("GPL");
