#ifndef _RK_PM_TESTS_H_
#define _RK_PM_TESTS_H_
#define PM_DBG(fmt, args...) printk(KERN_DEBUG "PM_TESTS_DBG:\t"fmt, ##args)
#define PM_ERR(fmt, args...) printk(KERN_ERR "PM_TESTS_ERR:\t"fmt, ##args)
#endif
