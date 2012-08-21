
/* porting layer */
/* Android */

#ifndef _MTK_PORTING_H_
#define _MTK_PORTING_H_

#include <linux/kernel.h> /* include stddef.h for NULL */

/* Type definition for signed integers */
typedef signed char INT8, *PINT8;
typedef signed short INT16, *PINT16;
typedef signed int INT32, *PINT32;

/* Type definition for unsigned integers */
typedef unsigned char UINT8, *PUINT8;
typedef unsigned short UINT16, *PUINT16;
typedef unsigned int UINT32, *PUINT32;

//typedef void VOID, *PVOID;

typedef int MTK_WCN_BOOL;
#ifndef MTK_WCN_BOOL_TRUE
#define MTK_WCN_BOOL_FALSE               ((MTK_WCN_BOOL) 0)
#define MTK_WCN_BOOL_TRUE                ((MTK_WCN_BOOL) 1)
#endif

typedef int MTK_WCN_MUTEX;

typedef int MTK_WCN_TIMER;

/* system APIs */
/* mutex */
typedef MTK_WCN_MUTEX (*MUTEX_CREATE)(const char * const name);
typedef INT32 (*MUTEX_DESTROY)(MTK_WCN_MUTEX mtx);
typedef INT32 (*MUTEX_LOCK)(MTK_WCN_MUTEX mtx);
typedef INT32 (*MUTEX_UNLOCK)(MTK_WCN_MUTEX mtx, unsigned long flags);
/* debug */
typedef INT32 (*DBG_PRINT)(const char *str, ...);
typedef INT32 (*DBG_ASSERT)(INT32 expr, const char *file, INT32 line);
/* timer */
typedef void (*MTK_WCN_TIMER_CB)(void);
typedef MTK_WCN_TIMER (*TIMER_CREATE)(const char * const name);
typedef INT32 (*TIMER_DESTROY)(MTK_WCN_TIMER tmr);
typedef INT32 (*TIMER_START)(MTK_WCN_TIMER tmr, UINT32 timeout, MTK_WCN_TIMER_CB tmr_cb, void *param);
typedef INT32 (*TIMER_STOP)(MTK_WCN_TIMER tmr);
/* kernel lib */
typedef void* (*SYS_MEMCPY)(void *dest, const void *src, UINT32 n);
typedef void* (*SYS_MEMSET)(void *s, INT32 c, UINT32 n);
typedef INT32 (*SYS_SPRINTF)(char *str, const char *format, ...);

#endif /* _MTK_PORTING_H_ */

