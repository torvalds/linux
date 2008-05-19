#ifndef __smskdefs_h__
#define __smskdefs_h__

#include <linux/version.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <asm/scatterlist.h>
#include <asm/page.h>

#include <linux/mutex.h>

typedef struct mutex kmutex_t;

#define kmutex_init(_p_) mutex_init(_p_)
#define kmutex_lock(_p_) mutex_lock(_p_)
#define kmutex_trylock(_p_) mutex_trylock(_p_)
#define kmutex_unlock(_p_) mutex_unlock(_p_)


#endif // __smskdefs_h__
