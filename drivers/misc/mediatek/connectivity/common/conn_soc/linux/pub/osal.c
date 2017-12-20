/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"
#include "osal.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/* CRC table for the CRC-16. The poly is 0x8005 (x^16 + x^15 + x^2 + 1) */
static UINT16 const crc16_table[256] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*string operations*/
_osal_inline_ UINT32 osal_strlen(const char *str)
{
	return strlen(str);
}

_osal_inline_ INT32 osal_strcmp(const char *dst, const char *src)
{
	return strcmp(dst, src);
}

_osal_inline_ INT32 osal_strncmp(const char *dst, const char *src, UINT32 len)
{
	return strncmp(dst, src, len);
}

_osal_inline_ char *osal_strcpy(char *dst, const char *src)
{
	return strcpy(dst, src);
}

_osal_inline_ char *osal_strncpy(char *dst, const char *src, UINT32 len)
{
	return strncpy(dst, src, len);
}

_osal_inline_ char *osal_strcat(char *dst, const char *src)
{
	return strcat(dst, src);
}

_osal_inline_ char *osal_strncat(char *dst, const char *src, UINT32 len)
{
	return strncat(dst, src, len);
}

_osal_inline_ char *osal_strchr(const char *str, UINT8 c)
{
	return strchr(str, c);
}

_osal_inline_ char *osal_strsep(char **str, const char *c)
{
	return strsep(str, c);
}

_osal_inline_ int osal_strtol(const char *str, UINT32 adecimal, long *res)
{
	return kstrtol(str, adecimal, res);
}

_osal_inline_ char *osal_strstr(char *str1, const char *str2)
{
	return strstr(str1, str2);
}

INT32 osal_snprintf(char *buf, UINT32 len, const char *fmt, ...)
{
	INT32 iRet = 0;
	va_list args;

	/*va_start(args, fmt); */
	va_start(args, fmt);
	/*iRet = snprintf(buf, len, fmt, args); */
	iRet = vsnprintf(buf, len, fmt, args);
	va_end(args);

	return iRet;
}

INT32 osal_err_print(const char *str, ...)
{
	va_list args;
	char tempString[DBG_LOG_STR_SIZE];

	va_start(args, str);
	vsnprintf(tempString, DBG_LOG_STR_SIZE, str, args);
	va_end(args);

	pr_err("%s", tempString);

	return 0;
}

INT32 osal_dbg_print(const char *str, ...)
{
	va_list args;
	char tempString[DBG_LOG_STR_SIZE];

	va_start(args, str);
	vsnprintf(tempString, DBG_LOG_STR_SIZE, str, args);
	va_end(args);

	pr_debug("%s", tempString);

	return 0;
}

INT32 osal_warn_print(const char *str, ...)
{
	va_list args;
	char tempString[DBG_LOG_STR_SIZE];

	va_start(args, str);
	vsnprintf(tempString, DBG_LOG_STR_SIZE, str, args);
	va_end(args);

	pr_warn("%s", tempString);

	return 0;
}

INT32 osal_dbg_assert(INT32 expr, const char *file, INT32 line)
{
	if (!expr) {
		pr_warn("%s (%d)\n", file, line);
		/*BUG_ON(!expr); */
#ifdef CFG_COMMON_GPIO_DBG_PIN
/* package this part */
		mt_set_gpio_out(GPIO70, GPIO_OUT_ZERO);
		pr_warn("toggle GPIO70\n");
		udelay(10);
		mt_set_gpio_out(GPIO70, GPIO_OUT_ONE);
#endif
		return 1;
	}
	return 0;

}

INT32 osal_dbg_assert_aee(const char *module, const char *detail_description)
{
	osal_err_print("[WMT-ASSERT]" "[E][Module]:%s, [INFO]%s\n", module, detail_description);

#ifdef WMT_PLAT_ALPS
	/* aee_kernel_warning(module,detail_description); */
	aee_kernel_warning_api(__FILE__, __LINE__, DB_OPT_WCN_ISSUE_INFO, module, detail_description);
#endif
	return 0;
}

INT32 osal_sprintf(char *str, const char *format, ...)
{
	INT32 iRet = 0;
	va_list args;

	va_start(args, format);
	iRet = vsnprintf(str, DBG_LOG_STR_SIZE, format, args);
	va_end(args);

	return iRet;
}

_osal_inline_ VOID *osal_malloc(UINT32 size)
{
	return vmalloc(size);
}

_osal_inline_ VOID osal_free(const VOID *dst)
{
	vfree(dst);
}

_osal_inline_ VOID *osal_memset(VOID *buf, INT32 i, UINT32 len)
{
	return memset(buf, i, len);
}

_osal_inline_ VOID *osal_memcpy(VOID *dst, const VOID *src, UINT32 len)
{
#ifdef CONFIG_MTK_WCN_ARM64
	char *tmp;
	const char *s;
	size_t i;

	tmp = dst;
	s = src;
	for (i = 0; i < len; i++)
		tmp[i] = s[i];

	return dst;

#else
	return memcpy(dst, src, len);
#endif
}

_osal_inline_ INT32 osal_memcmp(const VOID *buf1, const VOID *buf2, UINT32 len)
{
	return memcmp(buf1, buf2, len);
}

_osal_inline_ UINT16 osal_crc16(const UINT8 *buffer, const UINT32 length)
{
	UINT16 crc = 0;
	UINT32 i = 0;

	/* FIXME: Add STP checksum feature */
	crc = 0;
	for (i = 0; i < length; i++, buffer++)
		crc = (crc >> 8) ^ crc16_table[(crc ^ (*buffer)) & 0xff];

	return crc;
}

_osal_inline_ VOID osal_thread_show_stack(P_OSAL_THREAD pThread)
{
	return show_stack(pThread->pThread, NULL);
}

/*
  *OSAL layer Thread Opeartion related APIs
  *
  *
*/
_osal_inline_ INT32 osal_thread_create(P_OSAL_THREAD pThread)
{
	pThread->pThread = kthread_create(pThread->pThreadFunc, pThread->pThreadData, pThread->threadName);
	if (NULL == pThread->pThread)
		return -1;

	return 0;
}

_osal_inline_ INT32 osal_thread_run(P_OSAL_THREAD pThread)
{
	if (pThread->pThread) {
		wake_up_process(pThread->pThread);
		return 0;
	} else {
		return -1;
	}
}

_osal_inline_ INT32 osal_thread_stop(P_OSAL_THREAD pThread)
{
	INT32 iRet;

	if ((pThread) && (pThread->pThread)) {
		iRet = kthread_stop(pThread->pThread);
		/* pThread->pThread = NULL; */
		return iRet;
	}
	return -1;
}

_osal_inline_ INT32 osal_thread_should_stop(P_OSAL_THREAD pThread)
{
	if ((pThread) && (pThread->pThread))
		return kthread_should_stop();
	else
		return 1;

}

_osal_inline_ INT32
osal_thread_wait_for_event(P_OSAL_THREAD pThread, P_OSAL_EVENT pEvent, P_OSAL_EVENT_CHECKER pChecker)
{
	/*  P_DEV_WMT pDevWmt;*/

	if ((pThread) && (pThread->pThread) && (pEvent) && (pChecker)) {
		/* pDevWmt = (P_DEV_WMT)(pThread->pThreadData);*/
		return wait_event_interruptible(pEvent->waitQueue, (/*!RB_EMPTY(&pDevWmt->rActiveOpQ) || */
									   osal_thread_should_stop(pThread)
									   || (*pChecker) (pThread)));
	}
	return -1;
}

_osal_inline_ INT32 osal_thread_destroy(P_OSAL_THREAD pThread)
{
	if (pThread && (pThread->pThread)) {
		kthread_stop(pThread->pThread);
		pThread->pThread = NULL;
	}
	return 0;
}

/*
  *OSAL layer Signal Opeartion related APIs
  *initialization
  *wait for signal
  *wait for signal timerout
  *raise signal
  *destroy a signal
  *
*/

_osal_inline_ INT32 osal_signal_init(P_OSAL_SIGNAL pSignal)
{
	if (pSignal) {
		init_completion(&pSignal->comp);
		return 0;
	} else {
		return -1;
	}
}

_osal_inline_ INT32 osal_wait_for_signal(P_OSAL_SIGNAL pSignal)
{
	if (pSignal) {
		wait_for_completion_interruptible(&pSignal->comp);
		return 0;
	} else {
		return -1;
	}
}

_osal_inline_ INT32 osal_wait_for_signal_timeout(P_OSAL_SIGNAL pSignal)
{
	/* return wait_for_completion_interruptible_timeout(&pSignal->comp, msecs_to_jiffies(pSignal->timeoutValue)); */
	/* [ChangeFeature][George] gps driver may be closed by -ERESTARTSYS.
	 * Avoid using *interruptible" version in order to complete our jobs, such
	 * as function off gracefully.
	 */
	return wait_for_completion_timeout(&pSignal->comp, msecs_to_jiffies(pSignal->timeoutValue));
}

_osal_inline_ INT32 osal_raise_signal(P_OSAL_SIGNAL pSignal)
{
	/* TODO:[FixMe][GeorgeKuo]: DO sanity check here!!! */
	complete(&pSignal->comp);
	return 0;
}

_osal_inline_ INT32 osal_signal_deinit(P_OSAL_SIGNAL pSignal)
{
	/* TODO:[FixMe][GeorgeKuo]: DO sanity check here!!! */
	pSignal->timeoutValue = 0;
	return 0;
}

/*
  *OSAL layer Event Opeartion related APIs
  *initialization
  *wait for signal
  *wait for signal timerout
  *raise signal
  *destroy a signal
  *
*/

INT32 osal_event_init(P_OSAL_EVENT pEvent)
{
	init_waitqueue_head(&pEvent->waitQueue);

	return 0;
}

INT32 osal_wait_for_event(P_OSAL_EVENT pEvent, INT32(*condition) (PVOID), void *cond_pa)
{
	return wait_event_interruptible(pEvent->waitQueue, condition(cond_pa));
}

INT32 osal_wait_for_event_timeout(P_OSAL_EVENT pEvent, INT32(*condition) (PVOID), void *cond_pa)
{
	return wait_event_interruptible_timeout(pEvent->waitQueue, condition(cond_pa),
						msecs_to_jiffies(pEvent->timeoutValue));
}

INT32 osal_trigger_event(P_OSAL_EVENT pEvent)
{
	INT32 ret = 0;

	wake_up_interruptible(&pEvent->waitQueue);
	return ret;
}

INT32 osal_event_deinit(P_OSAL_EVENT pEvent)
{
	return 0;
}

_osal_inline_ long osal_wait_for_event_bit_set(P_OSAL_EVENT pEvent, unsigned long *pState, UINT32 bitOffset)
{
	UINT32 ms = pEvent->timeoutValue;

	if (ms != 0) {
		return wait_event_interruptible_timeout(pEvent->waitQueue, test_bit(bitOffset, pState),
							msecs_to_jiffies(ms));
	} else {
		return wait_event_interruptible(pEvent->waitQueue, test_bit(bitOffset, pState));
	}

}

_osal_inline_ long osal_wait_for_event_bit_clr(P_OSAL_EVENT pEvent, unsigned long *pState, UINT32 bitOffset)
{
	UINT32 ms = pEvent->timeoutValue;

	if (ms != 0) {
		return wait_event_interruptible_timeout(pEvent->waitQueue, !test_bit(bitOffset, pState),
							msecs_to_jiffies(ms));
	} else {
		return wait_event_interruptible(pEvent->waitQueue, !test_bit(bitOffset, pState));
	}

}

/*
  *bit test and set/clear operations APIs
  *
  *
*/
#if    OS_BIT_OPS_SUPPORT
#define osal_bit_op_lock(x)
#define osal_bit_op_unlock(x)
#else

_osal_inline_ INT32 osal_bit_op_lock(P_OSAL_UNSLEEPABLE_LOCK pLock)
{

	return 0;
}

_osal_inline_ INT32 osal_bit_op_unlock(P_OSAL_UNSLEEPABLE_LOCK pLock)
{

	return 0;
}
#endif
_osal_inline_ INT32 osal_clear_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData)
{
	osal_bit_op_lock(&(pData->opLock));
	clear_bit(bitOffset, &pData->data);
	osal_bit_op_unlock(&(pData->opLock));
	return 0;
}

_osal_inline_ INT32 osal_set_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData)
{
	osal_bit_op_lock(&(pData->opLock));
	set_bit(bitOffset, &pData->data);
	osal_bit_op_unlock(&(pData->opLock));
	return 0;
}

_osal_inline_ INT32 osal_test_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData)
{
	UINT32 iRet = 0;

	osal_bit_op_lock(&(pData->opLock));
	iRet = test_bit(bitOffset, &pData->data);
	osal_bit_op_unlock(&(pData->opLock));
	return iRet;
}

_osal_inline_ INT32 osal_test_and_clear_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData)
{
	UINT32 iRet = 0;

	osal_bit_op_lock(&(pData->opLock));
	iRet = test_and_clear_bit(bitOffset, &pData->data);
	osal_bit_op_unlock(&(pData->opLock));
	return iRet;

}

_osal_inline_ INT32 osal_test_and_set_bit(UINT32 bitOffset, P_OSAL_BIT_OP_VAR pData)
{
	UINT32 iRet = 0;

	osal_bit_op_lock(&(pData->opLock));
	iRet = test_and_set_bit(bitOffset, &pData->data);
	osal_bit_op_unlock(&(pData->opLock));
	return iRet;
}

/*
  *tiemr operations APIs
  *create
  *stop
  * modify
  *create
  *delete
  *
*/

INT32 osal_timer_create(P_OSAL_TIMER pTimer)
{
	struct timer_list *timer = &pTimer->timer;

	init_timer(timer);
	timer->function = pTimer->timeoutHandler;
	timer->data = (unsigned long)pTimer->timeroutHandlerData;
	return 0;
}

INT32 osal_timer_start(P_OSAL_TIMER pTimer, UINT32 ms)
{

	struct timer_list *timer = &pTimer->timer;

	timer->expires = jiffies + (ms / (1000 / HZ));
	add_timer(timer);
	return 0;
}

INT32 osal_timer_stop(P_OSAL_TIMER pTimer)
{
	struct timer_list *timer = &pTimer->timer;

	del_timer(timer);
	return 0;
}

INT32 osal_timer_stop_sync(P_OSAL_TIMER pTimer)
{
	struct timer_list *timer = &pTimer->timer;

	del_timer_sync(timer);
	return 0;
}

INT32 osal_timer_modify(P_OSAL_TIMER pTimer, UINT32 ms)
{

	mod_timer(&pTimer->timer, jiffies + (ms) / (1000 / HZ));
	return 0;
}

INT32 _osal_fifo_init(OSAL_FIFO *pFifo, UINT8 *buf, UINT32 size)
{
	struct kfifo *fifo = NULL;
	INT32 ret = -1;

	if (!pFifo) {
		pr_err("pFifo must be !NULL\n");
		return -1;
	}
	if (pFifo->pFifoBody) {
		pr_err("pFifo->pFifoBody must be NULL\n");
		pr_err("pFifo(0x%p), pFifo->pFifoBody(0x%p)\n", pFifo, pFifo->pFifoBody);
		return -1;
	}
	fifo = kzalloc(sizeof(struct kfifo), GFP_ATOMIC);
	if (!buf) {
		/*fifo's buffer is not ready, we allocate automatically */
		ret = kfifo_alloc(fifo, size, /*GFP_KERNEL */ GFP_ATOMIC);
	} else {
		if (is_power_of_2(size)) {
			kfifo_init(fifo, buf, size);
			ret = 0;
		} else {
			kfifo_free(fifo);
			fifo = NULL;
			ret = -1;
		}
	}

	pFifo->pFifoBody = fifo;
	return (ret < 0) ? (-1) : (0);
}

INT32 _osal_fifo_deinit(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		kfifo_free(fifo);

	return 0;
}

INT32 _osal_fifo_size(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		ret = kfifo_size(fifo);

	return ret;
}

/*returns unused bytes in fifo*/
INT32 _osal_fifo_avail_size(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		ret = kfifo_avail(fifo);

	return ret;
}

/*returns used bytes in fifo*/
INT32 _osal_fifo_len(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		ret = kfifo_len(fifo);

	return ret;
}

INT32 _osal_fifo_is_empty(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		ret = kfifo_is_empty(fifo);

	return ret;
}

INT32 _osal_fifo_is_full(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		ret = kfifo_is_full(fifo);

	return ret;
}

INT32 _osal_fifo_data_in(OSAL_FIFO *pFifo, const VOID *buf, UINT32 len)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo && buf && (len <= _osal_fifo_avail_size(pFifo))) {
		ret = kfifo_in(fifo, buf, len);
	} else {
		pr_err("%s: kfifo_in, error, len = %d, _osal_fifo_avail_size = %d, buf=%p\n",
		       __func__, len, _osal_fifo_avail_size(pFifo), buf);

		ret = 0;
	}

	return ret;
}

INT32 _osal_fifo_data_out(OSAL_FIFO *pFifo, void *buf, UINT32 len)
{
	struct kfifo *fifo = NULL;
	INT32 ret = 0;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo && buf && (len <= _osal_fifo_len(pFifo))) {
		ret = kfifo_out(fifo, buf, len);
	} else {
		pr_err("%s: kfifo_out, error, len = %d, osal_fifo_len = %d, buf=%p\n",
		       __func__, len, _osal_fifo_len(pFifo), buf);

		ret = 0;
	}

	return ret;
}

INT32 _osal_fifo_reset(OSAL_FIFO *pFifo)
{
	struct kfifo *fifo = NULL;

	if (!pFifo || !pFifo->pFifoBody) {
		pr_err("%s:pFifo = NULL or pFifo->pFifoBody = NULL, error\n", __func__);
		return -1;
	}

	fifo = (struct kfifo *)pFifo->pFifoBody;

	if (fifo)
		kfifo_reset(fifo);

	return 0;
}

INT32 osal_fifo_init(P_OSAL_FIFO pFifo, UINT8 *buffer, UINT32 size)
{
	if (!pFifo) {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		return -1;
	}

	pFifo->FifoInit = _osal_fifo_init;
	pFifo->FifoDeInit = _osal_fifo_deinit;
	pFifo->FifoSz = _osal_fifo_size;
	pFifo->FifoAvailSz = _osal_fifo_avail_size;
	pFifo->FifoLen = _osal_fifo_len;
	pFifo->FifoIsEmpty = _osal_fifo_is_empty;
	pFifo->FifoIsFull = _osal_fifo_is_full;
	pFifo->FifoDataIn = _osal_fifo_data_in;
	pFifo->FifoDataOut = _osal_fifo_data_out;
	pFifo->FifoReset = _osal_fifo_reset;

	if (NULL != pFifo->pFifoBody) {
		pr_err("%s:Because pFifo room is avialable, we clear the room and allocate them again.\n", __func__);
		pFifo->FifoDeInit(pFifo->pFifoBody);
		pFifo->pFifoBody = NULL;
	}

	pFifo->FifoInit(pFifo, buffer, size);

	return 0;
}

VOID osal_fifo_deinit(P_OSAL_FIFO pFifo)
{
	if (pFifo)
		pFifo->FifoDeInit(pFifo);
	else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		return;
	}
	kfree(pFifo->pFifoBody);
}

INT32 osal_fifo_reset(P_OSAL_FIFO pFifo)
{
	INT32 ret = -1;

	if (pFifo) {
		ret = pFifo->FifoReset(pFifo);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = -1;
	}
	return ret;
}

UINT32 osal_fifo_in(P_OSAL_FIFO pFifo, PUINT8 buffer, UINT32 size)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoDataIn(pFifo, buffer, size);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}

	return ret;
}

UINT32 osal_fifo_out(P_OSAL_FIFO pFifo, PUINT8 buffer, UINT32 size)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoDataOut(pFifo, buffer, size);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}

	return ret;
}

UINT32 osal_fifo_len(P_OSAL_FIFO pFifo)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoLen(pFifo);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}

	return ret;
}

UINT32 osal_fifo_sz(P_OSAL_FIFO pFifo)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoSz(pFifo);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}

	return ret;
}

UINT32 osal_fifo_avail(P_OSAL_FIFO pFifo)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoAvailSz(pFifo);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}

	return ret;
}

UINT32 osal_fifo_is_empty(P_OSAL_FIFO pFifo)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoIsEmpty(pFifo);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}

	return ret;
}

UINT32 osal_fifo_is_full(P_OSAL_FIFO pFifo)
{
	UINT32 ret = 0;

	if (pFifo) {
		ret = pFifo->FifoIsFull(pFifo);
	} else {
		pr_err("%s:pFifo = NULL, error\n", __func__);
		ret = 0;
	}
	return ret;
}

INT32 osal_wake_lock_init(P_OSAL_WAKE_LOCK pLock)
{
	if (!pLock)
		return -1;

	#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_init(&pLock->wake_lock, pLock->name);
	#else
	wake_lock_init(&pLock->wake_lock, WAKE_LOCK_SUSPEND, pLock->name);
	#endif
	return 0;
}

INT32 osal_wake_lock_deinit(P_OSAL_WAKE_LOCK pLock)
{
	if (!pLock)
		return -1;

	#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_trash(&pLock->wake_lock);
	#else
	wake_lock_destroy(&pLock->wake_lock);
	#endif
	return 0;
}

INT32 osal_wake_lock(P_OSAL_WAKE_LOCK pLock)
{
	if (!pLock)
		return -1;

	#ifdef CONFIG_PM_WAKELOCKS
	__pm_stay_awake(&pLock->wake_lock);
	#else
	wake_lock(&pLock->wake_lock);
	#endif

	return 0;
}

INT32 osal_wake_unlock(P_OSAL_WAKE_LOCK pLock)
{
	if (!pLock)
		return -1;

	#ifdef CONFIG_PM_WAKELOCKS
	__pm_relax(&pLock->wake_lock);
	#else
	wake_unlock(&pLock->wake_lock);
	#endif

	return 0;

}

INT32 osal_wake_lock_count(P_OSAL_WAKE_LOCK pLock)
{
	INT32 count = 0;

	if (!pLock)
		return -1;

	#ifdef CONFIG_PM_WAKELOCKS
	count = pLock->wake_lock.active;
	#else
	count = wake_lock_active(&pLock->wake_lock);
	#endif
	return count;
}

/*
  *sleepable lock operations APIs
  *init
  *lock
  *unlock
  *destroy
  *
*/

#if !defined(CONFIG_PROVE_LOCKING)
INT32 osal_unsleepable_lock_init(P_OSAL_UNSLEEPABLE_LOCK pUSL)
{
	spin_lock_init(&(pUSL->lock));
	return 0;
}
#endif

INT32 osal_lock_unsleepable_lock(P_OSAL_UNSLEEPABLE_LOCK pUSL)
{
	spin_lock_irqsave(&(pUSL->lock), pUSL->flag);
	return 0;
}

INT32 osal_unlock_unsleepable_lock(P_OSAL_UNSLEEPABLE_LOCK pUSL)
{
	spin_unlock_irqrestore(&(pUSL->lock), pUSL->flag);
	return 0;
}

INT32 osal_unsleepable_lock_deinit(P_OSAL_UNSLEEPABLE_LOCK pUSL)
{
	return 0;
}

/*
  *unsleepable operations APIs
  *init
  *lock
  *unlock
  *destroy

  *
*/

#if !defined(CONFIG_PROVE_LOCKING)
INT32 osal_sleepable_lock_init(P_OSAL_SLEEPABLE_LOCK pSL)
{
	mutex_init(&pSL->lock);
	return 0;
}
#endif

INT32 osal_lock_sleepable_lock(P_OSAL_SLEEPABLE_LOCK pSL)
{
	return mutex_lock_killable(&pSL->lock);
}

INT32 osal_unlock_sleepable_lock(P_OSAL_SLEEPABLE_LOCK pSL)
{
	mutex_unlock(&pSL->lock);
	return 0;
}

INT32 osal_sleepable_lock_deinit(P_OSAL_SLEEPABLE_LOCK pSL)
{
	mutex_destroy(&pSL->lock);
	return 0;
}

INT32 osal_sleep_ms(UINT32 ms)
{
	msleep(ms);
	return 0;
}

INT32 osal_udelay(UINT32 us)
{
	udelay(us);
	return 0;
}

INT32 osal_gettimeofday(PINT32 sec, PINT32 usec)
{
	INT32 ret = 0;
	struct timeval now;

	do_gettimeofday(&now);

	if (sec != NULL)
		*sec = now.tv_sec;
	else
		ret = -1;

	if (usec != NULL)
		*usec = now.tv_usec;
	else
		ret = -1;

	return ret;
}

INT32 osal_printtimeofday(const PUINT8 prefix)
{
	INT32 ret;
	INT32 sec;
	INT32 usec;

	ret = osal_gettimeofday(&sec, &usec);
	ret += osal_dbg_print("%s>sec=%d, usec=%d\n", prefix, sec, usec);

	return ret;
}

VOID osal_buffer_dump(const UINT8 *buf, const UINT8 *title, const UINT32 len, const UINT32 limit)
{
	INT32 k;
	UINT32 dump_len;

	pr_warn("start of dump>[%s] len=%d, limit=%d,", title, len, limit);

	dump_len = ((0 != limit) && (len > limit)) ? limit : len;
#if 0
	if (limit != 0)
		len = (len > limit) ? (limit) : (len);

#endif

	for (k = 0; k < dump_len; k++) {
		if ((k != 0) && (k % 16 == 0))
			pr_cont("\n");
		pr_cont("0x%02x ", buf[k]);
	}
	pr_warn("<end of dump\n");
}

UINT32 osal_op_get_id(P_OSAL_OP pOp)
{
	return (pOp) ? pOp->op.opId : 0xFFFFFFFF;
}

MTK_WCN_BOOL osal_op_is_wait_for_signal(P_OSAL_OP pOp)
{
	return (pOp && pOp->signal.timeoutValue) ? MTK_WCN_BOOL_TRUE : MTK_WCN_BOOL_FALSE;
}

VOID osal_op_raise_signal(P_OSAL_OP pOp, INT32 result)
{
	if (pOp) {
		pOp->result = result;
		osal_raise_signal(&pOp->signal);
	}
}

VOID osal_set_op_result(P_OSAL_OP pOp, INT32 result)
{
	if (pOp)
		pOp->result = result;

}
