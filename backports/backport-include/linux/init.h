#ifndef __BACKPORT_INIT_H
#define __BACKPORT_INIT_H
#include_next <linux/init.h>

/*
 * Backports 312b1485fb509c9bc32eda28ad29537896658cb8
 * Author: Sam Ravnborg <sam@ravnborg.org>
 * Date:   Mon Jan 28 20:21:15 2008 +0100
 * 
 * Introduce new section reference annotations tags: __ref, __refdata, __refconst
 */
#ifndef __ref
#define __ref		__init_refok
#endif
#ifndef __refdata
#define __refdata	__initdata_refok
#endif

#endif /* __BACKPORT_INIT_H */
