#ifndef _XGI_H
#define _XGI_H

#if 1
#define TWDEBUG(x)
#else
#define TWDEBUG(x) printk(KERN_INFO x "\n");
#endif

#endif
