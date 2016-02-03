#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>

#ifdef DWC_CCLIB
# include "dwc_cc.h"
#endif

#ifdef DWC_CRYPTOLIB
# include "dwc_modpow.h"
# include "dwc_dh.h"
# include "dwc_crypto.h"
#endif

#ifdef DWC_NOTIFYLIB
# include "dwc_notifier.h"
#endif

/* OS-Level Implementations */

/* This is the Linux kernel implementation of the DWC platform library. */
#include <linux/moduleparam.h>
#include <linux/ctype.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/usb.h>

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
# include <linux/usb/gadget.h>
#else
# include <linux/usb_gadget.h>
#endif

#include <asm/io.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include "dwc_os.h"
#include "dwc_list.h"

/** Prefix string for DWC_DEBUG print macros. */


/* MISC */

void *DWC_MEMSET(void *dest, uint8_t byte, uint32_t size)
{
	return memset(dest, byte, size);
}

void *DWC_MEMCPY(void *dest, void const *src, uint32_t size)
{
	return memcpy(dest, src, size);
}

void *DWC_MEMMOVE(void *dest, void *src, uint32_t size)
{
	return memmove(dest, src, size);
}

int DWC_MEMCMP(void *m1, void *m2, uint32_t size)
{
	return memcmp(m1, m2, size);
}

int DWC_STRNCMP(void *s1, void *s2, uint32_t size)
{
	return strncmp(s1, s2, size);
}

int DWC_STRCMP(void *s1, void *s2)
{
	return strcmp(s1, s2);
}

int DWC_STRLEN(char const *str)
{
	return strlen(str);
}

char *DWC_STRCPY(char *to, char const *from)
{
	return strcpy(to, from);
}

char *DWC_STRDUP(char const *str)
{
	int len = DWC_STRLEN(str) + 1;
	char *new = DWC_ALLOC_ATOMIC(len);

	if (!new) {
		return NULL;
	}

	DWC_MEMCPY(new, str, len);
	return new;
}

int DWC_ATOI(const char *str, int32_t *value)
{
	char *end = NULL;

	*value = simple_strtol(str, &end, 0);
	if (*end == '\0') {
		return 0;
	}

	return -1;
}

int DWC_ATOUI(const char *str, uint32_t *value)
{
	char *end = NULL;

	*value = simple_strtoul(str, &end, 0);
	if (*end == '\0') {
		return 0;
	}

	return -1;
}


#ifdef DWC_UTFLIB
/* From usbstring.c */

int DWC_UTF8_TO_UTF16LE(uint8_t const *s, uint16_t *cp, unsigned len)
{
	int	count = 0;
	u8	c;
	u16	uchar;

	/* this insists on correct encodings, though not minimal ones.
	 * BUT it currently rejects legit 4-byte UTF-8 code points,
	 * which need surrogate pairs.  (Unicode 3.1 can use them.)
	 */
	while (len != 0 && (c = (u8) *s++) != 0) {
		if (unlikely(c & 0x80)) {
			/* 2-byte sequence: */
			/* 00000yyyyyxxxxxx = 110yyyyy 10xxxxxx */
			if ((c & 0xe0) == 0xc0) {
				uchar = (c & 0x1f) << 6;

				c = (u8) *s++;
				if ((c & 0xc0) != 0xc0)
					goto fail;
				c &= 0x3f;
				uchar |= c;

			/* 3-byte sequence (most CJKV characters): */
			/* zzzzyyyyyyxxxxxx = 1110zzzz 10yyyyyy 10xxxxxx */
			} else if ((c & 0xf0) == 0xe0) {
				uchar = (c & 0x0f) << 12;

				c = (u8) *s++;
				if ((c & 0xc0) != 0xc0)
					goto fail;
				c &= 0x3f;
				uchar |= c << 6;

				c = (u8) *s++;
				if ((c & 0xc0) != 0xc0)
					goto fail;
				c &= 0x3f;
				uchar |= c;

				/* no bogus surrogates */
				if (0xd800 <= uchar && uchar <= 0xdfff)
					goto fail;

			/* 4-byte sequence (surrogate pairs, currently rare): */
			/* 11101110wwwwzzzzyy + 110111yyyyxxxxxx */
			/*     = 11110uuu 10uuzzzz 10yyyyyy 10xxxxxx */
			/* (uuuuu = wwww + 1) */
			/*  FIXME accept the surrogate code points (only) */
			} else
				goto fail;
		} else
			uchar = c;
		put_unaligned (cpu_to_le16 (uchar), cp++);
		count++;
		len--;
	}
	return count;
fail:
	return -1;
}
#endif	/* DWC_UTFLIB */


/* dwc_debug.h */

dwc_bool_t DWC_IN_IRQ(void)
{
	return in_irq();
}

dwc_bool_t DWC_IN_BH(void)
{
	return in_softirq();
}

void DWC_VPRINTF(char *format, va_list args)
{
	vprintk(format, args);
}

int DWC_VSNPRINTF(char *str, int size, char *format, va_list args)
{
	return vsnprintf(str, size, format, args);
}

void DWC_PRINTF(char *format, ...)
{
	va_list args;

	va_start(args, format);
	DWC_VPRINTF(format, args);
	va_end(args);
}

int DWC_SPRINTF(char *buffer, char *format, ...)
{
	int retval;
	va_list args;

	va_start(args, format);
	retval = vsprintf(buffer, format, args);
	va_end(args);
	return retval;
}

int DWC_SNPRINTF(char *buffer, int size, char *format, ...)
{
	int retval;
	va_list args;

	va_start(args, format);
	retval = vsnprintf(buffer, size, format, args);
	va_end(args);
	return retval;
}

void __DWC_WARN(char *format, ...)
{
	va_list args;

	va_start(args, format);
	DWC_PRINTF(KERN_WARNING);
	DWC_VPRINTF(format, args);
	va_end(args);
}

void __DWC_ERROR(char *format, ...)
{
	va_list args;

	va_start(args, format);
	DWC_PRINTF(KERN_ERR);
	DWC_VPRINTF(format, args);
	va_end(args);
}

void DWC_EXCEPTION(char *format, ...)
{
	va_list args;

	va_start(args, format);
	DWC_PRINTF(KERN_ERR);
	DWC_VPRINTF(format, args);
	va_end(args);
	BUG_ON(1);
}

#ifdef DEBUG
void __DWC_DEBUG(char *format, ...)
{
	va_list args;

	va_start(args, format);
	DWC_PRINTF(KERN_ERR);
	DWC_VPRINTF(format, args);
	va_end(args);
}
#else
void __DWC_DEBUG(char *format, ...)
{
    ;
}
#endif



/* dwc_mem.h */

#if 0
dwc_pool_t *DWC_DMA_POOL_CREATE(uint32_t size,
				uint32_t align,
				uint32_t alloc)
{
	struct dma_pool *pool = dma_pool_create("Pool", NULL,
						size, align, alloc);
	return (dwc_pool_t *)pool;
}

void DWC_DMA_POOL_DESTROY(dwc_pool_t *pool)
{
	dma_pool_destroy((struct dma_pool *)pool);
}

void *DWC_DMA_POOL_ALLOC(dwc_pool_t *pool, uint64_t *dma_addr)
{
	return dma_pool_alloc((struct dma_pool *)pool, GFP_KERNEL, dma_addr);
}

void *DWC_DMA_POOL_ZALLOC(dwc_pool_t *pool, uint64_t *dma_addr)
{
	void *vaddr = DWC_DMA_POOL_ALLOC(pool, dma_addr);
	memset(..);
}

void DWC_DMA_POOL_FREE(dwc_pool_t *pool, void *vaddr, void *daddr)
{
	dma_pool_free(pool, vaddr, daddr);
}
#endif

void *__DWC_DMA_ALLOC(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr)
{
#if 1 /* def xxCOSIM  Only works for 32-bit cosim */
	void *buf = dma_alloc_coherent((struct device *)dma_ctx, (size_t)size,
				       dma_addr, GFP_KERNEL);
#else
	void *buf = dma_alloc_coherent(dma_ctx, (size_t)size, dma_addr, GFP_KERNEL | GFP_DMA32);
#endif
	if (!buf) {
		return NULL;
	}

	memset(buf, 0, (size_t)size);
	return buf;
}

void *__DWC_DMA_ALLOC_ATOMIC(void *dma_ctx, uint32_t size, dwc_dma_t *dma_addr)
{
	void *buf = dma_alloc_coherent((struct device *)dma_ctx, (size_t)size,
				       dma_addr, GFP_ATOMIC);
	if (!buf) {
		return NULL;
	}
	memset(buf, 0, (size_t)size);
	return buf;
}

void __DWC_DMA_FREE(void *dma_ctx, uint32_t size, void *virt_addr, dwc_dma_t dma_addr)
{
	dma_free_coherent((struct device *)dma_ctx, size, virt_addr, dma_addr);
}

void *__DWC_ALLOC(void *mem_ctx, uint32_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

void *__DWC_ALLOC_ATOMIC(void *mem_ctx, uint32_t size)
{
	return kzalloc(size, GFP_ATOMIC);
}

void __DWC_FREE(void *mem_ctx, void *addr)
{
	kfree(addr);
}


#ifdef DWC_CRYPTOLIB
/* dwc_crypto.h */

void DWC_RANDOM_BYTES(uint8_t *buffer, uint32_t length)
{
	get_random_bytes(buffer, length);
}

int DWC_AES_CBC(uint8_t *message, uint32_t messagelen, uint8_t *key, uint32_t keylen, uint8_t iv[16], uint8_t *out)
{
	struct crypto_blkcipher *tfm;
	struct blkcipher_desc desc;
	struct scatterlist sgd;
	struct scatterlist sgs;

	tfm = crypto_alloc_blkcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC);
	if (tfm == NULL) {
		printk("failed to load transform for aes CBC\n");
		return -1;
	}

	crypto_blkcipher_setkey(tfm, key, keylen);
	crypto_blkcipher_set_iv(tfm, iv, 16);

	sg_init_one(&sgd, out, messagelen);
	sg_init_one(&sgs, message, messagelen);

	desc.tfm = tfm;
	desc.flags = 0;

	if (crypto_blkcipher_encrypt(&desc, &sgd, &sgs, messagelen)) {
		crypto_free_blkcipher(tfm);
		DWC_ERROR("AES CBC encryption failed");
		return -1;
	}

	crypto_free_blkcipher(tfm);
	return 0;
}

int DWC_SHA256(uint8_t *message, uint32_t len, uint8_t *out)
{
	struct crypto_hash *tfm;
	struct hash_desc desc;
	struct scatterlist sg;

	tfm = crypto_alloc_hash("sha256", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		DWC_ERROR("Failed to load transform for sha256: %ld\n", PTR_ERR(tfm));
		return 0;
	}
	desc.tfm = tfm;
	desc.flags = 0;

	sg_init_one(&sg, message, len);
	crypto_hash_digest(&desc, &sg, len, out);
	crypto_free_hash(tfm);

	return 1;
}

int DWC_HMAC_SHA256(uint8_t *message, uint32_t messagelen,
		    uint8_t *key, uint32_t keylen, uint8_t *out)
{
	struct crypto_hash *tfm;
	struct hash_desc desc;
	struct scatterlist sg;

	tfm = crypto_alloc_hash("hmac(sha256)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		DWC_ERROR("Failed to load transform for hmac(sha256): %ld\n", PTR_ERR(tfm));
		return 0;
	}
	desc.tfm = tfm;
	desc.flags = 0;

	sg_init_one(&sg, message, messagelen);
	crypto_hash_setkey(tfm, key, keylen);
	crypto_hash_digest(&desc, &sg, messagelen, out);
	crypto_free_hash(tfm);

	return 1;
}
#endif	/* DWC_CRYPTOLIB */


/* Byte Ordering Conversions */

uint32_t DWC_CPU_TO_LE32(uint32_t *p)
{
#ifdef __LITTLE_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint32_t ret;
	ret = u_p[3] | (u_p[2] << 8) | (u_p[1] << 16) | (u_p[0] << 24);

	return ret;
#endif
}

uint32_t DWC_CPU_TO_BE32(uint32_t *p)
{
#ifdef __BIG_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint32_t ret;
	ret = u_p[3] | (u_p[2] << 8) | (u_p[1] << 16) | (u_p[0] << 24);

	return ret;
#endif
}

uint32_t DWC_LE32_TO_CPU(uint32_t *p)
{
#ifdef __LITTLE_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint32_t ret;
	ret = u_p[3] | (u_p[2] << 8) | (u_p[1] << 16) | (u_p[0] << 24);

	return ret;
#endif
}

uint32_t DWC_BE32_TO_CPU(uint32_t *p)
{
#ifdef __BIG_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint32_t ret;
	ret = u_p[3] | (u_p[2] << 8) | (u_p[1] << 16) | (u_p[0] << 24);

	return ret;
#endif
}

uint16_t DWC_CPU_TO_LE16(uint16_t *p)
{
#ifdef __LITTLE_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint16_t ret;
	ret = u_p[1] | (u_p[0] << 8);
	return ret;
#endif
}

uint16_t DWC_CPU_TO_BE16(uint16_t *p)
{
#ifdef __BIG_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint16_t ret;
	ret = u_p[1] | (u_p[0] << 8);
	return ret;
#endif
}

uint16_t DWC_LE16_TO_CPU(uint16_t *p)
{
#ifdef __LITTLE_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint16_t ret;
	ret = u_p[1] | (u_p[0] << 8);
	return ret;
#endif
}

uint16_t DWC_BE16_TO_CPU(uint16_t *p)
{
#ifdef __BIG_ENDIAN
	return *p;
#else
	uint8_t *u_p = (uint8_t *)p;
	uint16_t ret;
	ret = u_p[1] | (u_p[0] << 8);
	return ret;
#endif
}


/* Registers */

uint32_t DWC_READ_REG32(volatile uint32_t *reg)
{
	return readl_relaxed(reg);
}

#if 0
uint64_t DWC_READ_REG64(volatile uint64_t *reg)
{
}
#endif

void DWC_WRITE_REG32(volatile uint32_t *reg, uint32_t value)
{
	writel_relaxed(value, reg);
	dsb(sy);
}

#if 0
void DWC_WRITE_REG64(volatile uint64_t *reg, uint64_t value)
{
}
#endif

void DWC_MODIFY_REG32(volatile uint32_t *reg, uint32_t clear_mask, uint32_t set_mask)
{
	writel_relaxed((readl_relaxed(reg) & ~clear_mask) | set_mask, reg);
	dsb(sy);
}

#if 0
void DWC_MODIFY_REG64(volatile uint64_t *reg, uint64_t clear_mask, uint64_t set_mask)
{
}
#endif


/* Locking */

dwc_spinlock_t *DWC_SPINLOCK_ALLOC(void)
{
	spinlock_t *sl = (spinlock_t *)1;

#if defined(CONFIG_PREEMPT) || defined(CONFIG_SMP)
	sl = DWC_ALLOC(sizeof(*sl));
	if (!sl) {
		DWC_ERROR("Cannot allocate memory for spinlock\n");
		return NULL;
	}

	spin_lock_init(sl);
#endif
	return (dwc_spinlock_t *)sl;
}

void DWC_SPINLOCK_FREE(dwc_spinlock_t *lock)
{
#if defined(CONFIG_PREEMPT) || defined(CONFIG_SMP)
	DWC_FREE(lock);
#endif
}

void DWC_SPINLOCK(dwc_spinlock_t *lock)
{
#if defined(CONFIG_PREEMPT) || defined(CONFIG_SMP)
	spin_lock((spinlock_t *)lock);
#endif
}

void DWC_SPINUNLOCK(dwc_spinlock_t *lock)
{
#if defined(CONFIG_PREEMPT) || defined(CONFIG_SMP)
	spin_unlock((spinlock_t *)lock);
#endif
}

void DWC_SPINLOCK_IRQSAVE(dwc_spinlock_t *lock, dwc_irqflags_t *flags)
{
	dwc_irqflags_t f;

#if defined(CONFIG_PREEMPT) || defined(CONFIG_SMP)
	spin_lock_irqsave((spinlock_t *)lock, f);
#else
	local_irq_save(f);
#endif
	*flags = f;
}

void DWC_SPINUNLOCK_IRQRESTORE(dwc_spinlock_t *lock, dwc_irqflags_t flags)
{
#if defined(CONFIG_PREEMPT) || defined(CONFIG_SMP)
	spin_unlock_irqrestore((spinlock_t *)lock, flags);
#else
	local_irq_restore(flags);
#endif
}

dwc_mutex_t *DWC_MUTEX_ALLOC(void)
{
	struct mutex *m;
	dwc_mutex_t *mutex = (dwc_mutex_t *)DWC_ALLOC(sizeof(struct mutex));

	if (!mutex) {
		DWC_ERROR("Cannot allocate memory for mutex\n");
		return NULL;
	}

	m = (struct mutex *)mutex;
	mutex_init(m);
	return mutex;
}

#if (defined(DWC_LINUX) && defined(CONFIG_DEBUG_MUTEXES))
#else
void DWC_MUTEX_FREE(dwc_mutex_t *mutex)
{
	mutex_destroy((struct mutex *)mutex);
	DWC_FREE(mutex);
}
#endif

void DWC_MUTEX_LOCK(dwc_mutex_t *mutex)
{
	struct mutex *m = (struct mutex *)mutex;
	mutex_lock(m);
}

int DWC_MUTEX_TRYLOCK(dwc_mutex_t *mutex)
{
	struct mutex *m = (struct mutex *)mutex;
	return mutex_trylock(m);
}

void DWC_MUTEX_UNLOCK(dwc_mutex_t *mutex)
{
	struct mutex *m = (struct mutex *)mutex;
	mutex_unlock(m);
}


/* Timing */

void DWC_UDELAY(uint32_t usecs)
{
	udelay(usecs);
}

void DWC_MDELAY(uint32_t msecs)
{
	mdelay(msecs);
}

void DWC_MSLEEP(uint32_t msecs)
{
	msleep(msecs);
}

uint32_t DWC_TIME(void)
{
	return jiffies_to_msecs(jiffies);
}


/* Timers */

struct dwc_timer {
	struct timer_list *t;
	char *name;
	dwc_timer_callback_t cb;
	void *data;
	uint8_t scheduled;
	dwc_spinlock_t *lock;
};

static void timer_callback(unsigned long data)
{
	dwc_timer_t *timer = (dwc_timer_t *)data;
	dwc_irqflags_t flags;

	DWC_SPINLOCK_IRQSAVE(timer->lock, &flags);
	timer->scheduled = 0;
	DWC_SPINUNLOCK_IRQRESTORE(timer->lock, flags);
	/* DWC_DEBUG("Timer %s callback", timer->name); */
	timer->cb(timer->data);
}

dwc_timer_t *DWC_TIMER_ALLOC(char *name, dwc_timer_callback_t cb, void *data)
{
	dwc_timer_t *t = DWC_ALLOC(sizeof(*t));

	if (!t) {
		DWC_ERROR("Cannot allocate memory for timer");
		return NULL;
	}

	t->t = DWC_ALLOC(sizeof(*t->t));
	if (!t->t) {
		DWC_ERROR("Cannot allocate memory for timer->t");
		goto no_timer;
	}

	t->name = DWC_STRDUP(name);
	if (!t->name) {
		DWC_ERROR("Cannot allocate memory for timer->name");
		goto no_name;
	}

	t->lock = DWC_SPINLOCK_ALLOC();
	if (!t->lock) {
		DWC_ERROR("Cannot allocate memory for lock");
		goto no_lock;
	}

	t->scheduled = 0;
	t->t->expires = jiffies;
	setup_timer(t->t, timer_callback, (unsigned long)t);

	t->cb = cb;
	t->data = data;

	return t;

 no_lock:
	DWC_FREE(t->name);
 no_name:
	DWC_FREE(t->t);
 no_timer:
	DWC_FREE(t);
	return NULL;
}

void DWC_TIMER_FREE(dwc_timer_t *timer)
{
	dwc_irqflags_t flags;

	DWC_SPINLOCK_IRQSAVE(timer->lock, &flags);

	if (timer->scheduled) {
		del_timer(timer->t);
		timer->scheduled = 0;
	}

	DWC_SPINUNLOCK_IRQRESTORE(timer->lock, flags);
	DWC_SPINLOCK_FREE(timer->lock);
	DWC_FREE(timer->t);
	DWC_FREE(timer->name);
	DWC_FREE(timer);
}

void DWC_TIMER_SCHEDULE(dwc_timer_t *timer, uint32_t time)
{
	dwc_irqflags_t flags;

	DWC_SPINLOCK_IRQSAVE(timer->lock, &flags);

	if (!timer->scheduled) {
		timer->scheduled = 1;
		/* DWC_DEBUG("Scheduling timer %s to expire in +%d msec",
		 * 	     timer->name, time);*/
		timer->t->expires = jiffies + msecs_to_jiffies(time);
		add_timer(timer->t);
	} else {
		/* DWC_DEBUG("Modifying timer %s to expire in +%d msec",
		 * 	     timer->name, time);*/
		mod_timer(timer->t, jiffies + msecs_to_jiffies(time));
	}

	DWC_SPINUNLOCK_IRQRESTORE(timer->lock, flags);
}

void DWC_TIMER_CANCEL(dwc_timer_t *timer)
{
	del_timer(timer->t);
}


/* Wait Queues */

struct dwc_waitq {
	wait_queue_head_t queue;
	int abort;
};

dwc_waitq_t *DWC_WAITQ_ALLOC(void)
{
	dwc_waitq_t *wq = DWC_ALLOC(sizeof(*wq));

	if (!wq) {
		DWC_ERROR("Cannot allocate memory for waitqueue\n");
		return NULL;
	}

	init_waitqueue_head(&wq->queue);
	wq->abort = 0;
	return wq;
}

void DWC_WAITQ_FREE(dwc_waitq_t *wq)
{
	DWC_FREE(wq);
}

int32_t DWC_WAITQ_WAIT(dwc_waitq_t *wq, dwc_waitq_condition_t cond, void *data)
{
	int result = wait_event_interruptible(wq->queue,
					      cond(data) || wq->abort);
	if (result == -ERESTARTSYS) {
		wq->abort = 0;
		return -DWC_E_RESTART;
	}

	if (wq->abort == 1) {
		wq->abort = 0;
		return -DWC_E_ABORT;
	}

	wq->abort = 0;

	if (result == 0) {
		return 0;
	}

	return -DWC_E_UNKNOWN;
}

int32_t DWC_WAITQ_WAIT_TIMEOUT(dwc_waitq_t *wq, dwc_waitq_condition_t cond,
			       void *data, int32_t msecs)
{
	int32_t tmsecs;
	int result = wait_event_interruptible_timeout(wq->queue,
						      cond(data) || wq->abort,
						      msecs_to_jiffies(msecs));
	if (result == -ERESTARTSYS) {
		wq->abort = 0;
		return -DWC_E_RESTART;
	}

	if (wq->abort == 1) {
		wq->abort = 0;
		return -DWC_E_ABORT;
	}

	wq->abort = 0;

	if (result > 0) {
		tmsecs = jiffies_to_msecs(result);
		if (!tmsecs) {
			return 1;
		}

		return tmsecs;
	}

	if (result == 0) {
		return -DWC_E_TIMEOUT;
	}

	return -DWC_E_UNKNOWN;
}

void DWC_WAITQ_TRIGGER(dwc_waitq_t *wq)
{
	wq->abort = 0;
	wake_up_interruptible(&wq->queue);
}

void DWC_WAITQ_ABORT(dwc_waitq_t *wq)
{
	wq->abort = 1;
	wake_up_interruptible(&wq->queue);
}


/* Threading */

dwc_thread_t *DWC_THREAD_RUN(dwc_thread_function_t func, char *name, void *data)
{
	struct task_struct *thread = kthread_run(func, data, name);

	if (thread == ERR_PTR(-ENOMEM)) {
		return NULL;
	}

	return (dwc_thread_t *)thread;
}

int DWC_THREAD_STOP(dwc_thread_t *thread)
{
	return kthread_stop((struct task_struct *)thread);
}

dwc_bool_t DWC_THREAD_SHOULD_STOP(void)
{
	return kthread_should_stop();
}


/* tasklets
 - run in interrupt context (cannot sleep)
 - each tasklet runs on a single CPU
 - different tasklets can be running simultaneously on different CPUs
 */
struct dwc_tasklet {
	struct tasklet_struct t;
	dwc_tasklet_callback_t cb;
	void *data;
};

static void tasklet_callback(unsigned long data)
{
	dwc_tasklet_t *t = (dwc_tasklet_t *)data;
	t->cb(t->data);
}

dwc_tasklet_t *DWC_TASK_ALLOC(char *name, dwc_tasklet_callback_t cb, void *data)
{
	dwc_tasklet_t *t = DWC_ALLOC(sizeof(*t));

	if (t) {
		t->cb = cb;
		t->data = data;
		tasklet_init(&t->t, tasklet_callback, (unsigned long)t);
	} else {
		DWC_ERROR("Cannot allocate memory for tasklet\n");
	}

	return t;
}

void DWC_TASK_FREE(dwc_tasklet_t *task)
{
	DWC_FREE(task);
}

void DWC_TASK_SCHEDULE(dwc_tasklet_t *task)
{
	tasklet_schedule(&task->t);
}


/* workqueues
 - run in process context (can sleep)
 */
typedef struct work_container {
	dwc_work_callback_t cb;
	void *data;
	dwc_workq_t *wq;
	char *name;

#ifdef DEBUG
	DWC_CIRCLEQ_ENTRY(work_container) entry;
#endif
	struct delayed_work work;
} work_container_t;

#ifdef DEBUG
DWC_CIRCLEQ_HEAD(work_container_queue, work_container);
#endif

struct dwc_workq {
	struct workqueue_struct *wq;
	dwc_spinlock_t *lock;
	dwc_waitq_t *waitq;
	int pending;

#ifdef DEBUG
	struct work_container_queue entries;
#endif
};

static void do_work(struct work_struct *work)
{
	dwc_irqflags_t flags;
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	work_container_t *container = container_of(dw, struct work_container, work);
	dwc_workq_t *wq = container->wq;

	container->cb(container->data);

#ifdef DEBUG
	DWC_CIRCLEQ_REMOVE(&wq->entries, container, entry);
#endif
	/* DWC_DEBUG("Work done: %s, container=%p",
	 * 	     container->name, container); */
	if (container->name) {
		DWC_FREE(container->name);
	}
	DWC_FREE(container);

	DWC_SPINLOCK_IRQSAVE(wq->lock, &flags);
	wq->pending--;
	DWC_SPINUNLOCK_IRQRESTORE(wq->lock, flags);
	DWC_WAITQ_TRIGGER(wq->waitq);
}

static int work_done(void *data)
{
	dwc_workq_t *workq = (dwc_workq_t *)data;
	return workq->pending == 0;
}

int DWC_WORKQ_WAIT_WORK_DONE(dwc_workq_t *workq, int timeout)
{
	return DWC_WAITQ_WAIT_TIMEOUT(workq->waitq, work_done, workq, timeout);
}

dwc_workq_t *DWC_WORKQ_ALLOC(char *name)
{
	dwc_workq_t *wq = DWC_ALLOC(sizeof(*wq));

	if (!wq) {
		return NULL;
	}

	wq->wq = create_singlethread_workqueue(name);
	if (!wq->wq) {
		goto no_wq;
	}

	wq->pending = 0;

	wq->lock = DWC_SPINLOCK_ALLOC();
	if (!wq->lock) {
		goto no_lock;
	}

	wq->waitq = DWC_WAITQ_ALLOC();
	if (!wq->waitq) {
		goto no_waitq;
	}

#ifdef DEBUG
	DWC_CIRCLEQ_INIT(&wq->entries);
#endif
	return wq;

 no_waitq:
	DWC_SPINLOCK_FREE(wq->lock);
 no_lock:
	destroy_workqueue(wq->wq);
 no_wq:
	DWC_FREE(wq);

	return NULL;
}

void DWC_WORKQ_FREE(dwc_workq_t *wq)
{
#ifdef DEBUG
	if (wq->pending != 0) {
		struct work_container *wc;
		DWC_ERROR("Destroying work queue with pending work");
		DWC_CIRCLEQ_FOREACH(wc, &wq->entries, entry) {
			DWC_ERROR("Work %s still pending", wc->name);
		}
	}
#endif
	destroy_workqueue(wq->wq);
	DWC_SPINLOCK_FREE(wq->lock);
	DWC_WAITQ_FREE(wq->waitq);
	DWC_FREE(wq);
}

void DWC_WORKQ_SCHEDULE(dwc_workq_t *wq, dwc_work_callback_t cb, void *data,
			char *format, ...)
{
	dwc_irqflags_t flags;
	work_container_t *container;
	static char name[128];
	va_list args;

	va_start(args, format);
	DWC_VSNPRINTF(name, 128, format, args);
	va_end(args);

	DWC_SPINLOCK_IRQSAVE(wq->lock, &flags);
	wq->pending++;
	DWC_SPINUNLOCK_IRQRESTORE(wq->lock, flags);
	DWC_WAITQ_TRIGGER(wq->waitq);

	container = DWC_ALLOC_ATOMIC(sizeof(*container));
	if (!container) {
		DWC_ERROR("Cannot allocate memory for container\n");
		return;
	}

	container->name = DWC_STRDUP(name);
	if (!container->name) {
		DWC_ERROR("Cannot allocate memory for container->name\n");
		DWC_FREE(container);
		return;
	}

	container->cb = cb;
	container->data = data;
	container->wq = wq;
	/* DWC_DEBUG("Queueing work: %s, container=%p",
	 * 	     container->name, container);*/
	INIT_WORK(&container->work.work, do_work);

#ifdef DEBUG
	DWC_CIRCLEQ_INSERT_TAIL(&wq->entries, container, entry);
#endif
	queue_work(wq->wq, &container->work.work);
}

void DWC_WORKQ_SCHEDULE_DELAYED(dwc_workq_t *wq, dwc_work_callback_t cb,
				void *data, uint32_t time, char *format, ...)
{
	dwc_irqflags_t flags;
	work_container_t *container;
	static char name[128];
	va_list args;

	va_start(args, format);
	DWC_VSNPRINTF(name, 128, format, args);
	va_end(args);

	DWC_SPINLOCK_IRQSAVE(wq->lock, &flags);
	wq->pending++;
	DWC_SPINUNLOCK_IRQRESTORE(wq->lock, flags);
	DWC_WAITQ_TRIGGER(wq->waitq);

	container = DWC_ALLOC_ATOMIC(sizeof(*container));
	if (!container) {
		DWC_ERROR("Cannot allocate memory for container\n");
		return;
	}

	container->name = DWC_STRDUP(name);
	if (!container->name) {
		DWC_ERROR("Cannot allocate memory for container->name\n");
		DWC_FREE(container);
		return;
	}

	container->cb = cb;
	container->data = data;
	container->wq = wq;
	/* DWC_DEBUG("Queueing work: %s, container=%p",
	 * 	     container->name, container);*/
	INIT_DELAYED_WORK(&container->work, do_work);

#ifdef DEBUG
	DWC_CIRCLEQ_INSERT_TAIL(&wq->entries, container, entry);
#endif
	queue_delayed_work(wq->wq, &container->work, msecs_to_jiffies(time));
}

int DWC_WORKQ_PENDING(dwc_workq_t *wq)
{
	return wq->pending;
}


#ifdef DWC_LIBMODULE

#ifdef DWC_CCLIB
/* CC */
EXPORT_SYMBOL(dwc_cc_if_alloc);
EXPORT_SYMBOL(dwc_cc_if_free);
EXPORT_SYMBOL(dwc_cc_clear);
EXPORT_SYMBOL(dwc_cc_add);
EXPORT_SYMBOL(dwc_cc_remove);
EXPORT_SYMBOL(dwc_cc_change);
EXPORT_SYMBOL(dwc_cc_data_for_save);
EXPORT_SYMBOL(dwc_cc_restore_from_data);
EXPORT_SYMBOL(dwc_cc_match_chid);
EXPORT_SYMBOL(dwc_cc_match_cdid);
EXPORT_SYMBOL(dwc_cc_ck);
EXPORT_SYMBOL(dwc_cc_chid);
EXPORT_SYMBOL(dwc_cc_cdid);
EXPORT_SYMBOL(dwc_cc_name);
#endif	/* DWC_CCLIB */

#ifdef DWC_CRYPTOLIB
# ifndef CONFIG_MACH_IPMATE
/* Modpow */
EXPORT_SYMBOL(dwc_modpow);

/* DH */
EXPORT_SYMBOL(dwc_dh_modpow);
EXPORT_SYMBOL(dwc_dh_derive_keys);
EXPORT_SYMBOL(dwc_dh_pk);
# endif	/* CONFIG_MACH_IPMATE */

/* Crypto */
EXPORT_SYMBOL(dwc_wusb_aes_encrypt);
EXPORT_SYMBOL(dwc_wusb_cmf);
EXPORT_SYMBOL(dwc_wusb_prf);
EXPORT_SYMBOL(dwc_wusb_fill_ccm_nonce);
EXPORT_SYMBOL(dwc_wusb_gen_nonce);
EXPORT_SYMBOL(dwc_wusb_gen_key);
EXPORT_SYMBOL(dwc_wusb_gen_mic);
#endif	/* DWC_CRYPTOLIB */

/* Notification */
#ifdef DWC_NOTIFYLIB
EXPORT_SYMBOL(dwc_alloc_notification_manager);
EXPORT_SYMBOL(dwc_free_notification_manager);
EXPORT_SYMBOL(dwc_register_notifier);
EXPORT_SYMBOL(dwc_unregister_notifier);
EXPORT_SYMBOL(dwc_add_observer);
EXPORT_SYMBOL(dwc_remove_observer);
EXPORT_SYMBOL(dwc_notify);
#endif

/* Memory Debugging Routines */
#ifdef DWC_DEBUG_MEMORY
EXPORT_SYMBOL(dwc_alloc_debug);
EXPORT_SYMBOL(dwc_alloc_atomic_debug);
EXPORT_SYMBOL(dwc_free_debug);
EXPORT_SYMBOL(dwc_dma_alloc_debug);
EXPORT_SYMBOL(dwc_dma_free_debug);
#endif

EXPORT_SYMBOL(DWC_MEMSET);
EXPORT_SYMBOL(DWC_MEMCPY);
EXPORT_SYMBOL(DWC_MEMMOVE);
EXPORT_SYMBOL(DWC_MEMCMP);
EXPORT_SYMBOL(DWC_STRNCMP);
EXPORT_SYMBOL(DWC_STRCMP);
EXPORT_SYMBOL(DWC_STRLEN);
EXPORT_SYMBOL(DWC_STRCPY);
EXPORT_SYMBOL(DWC_STRDUP);
EXPORT_SYMBOL(DWC_ATOI);
EXPORT_SYMBOL(DWC_ATOUI);

#ifdef DWC_UTFLIB
EXPORT_SYMBOL(DWC_UTF8_TO_UTF16LE);
#endif	/* DWC_UTFLIB */

EXPORT_SYMBOL(DWC_IN_IRQ);
EXPORT_SYMBOL(DWC_IN_BH);
EXPORT_SYMBOL(DWC_VPRINTF);
EXPORT_SYMBOL(DWC_VSNPRINTF);
EXPORT_SYMBOL(DWC_PRINTF);
EXPORT_SYMBOL(DWC_SPRINTF);
EXPORT_SYMBOL(DWC_SNPRINTF);
EXPORT_SYMBOL(__DWC_WARN);
EXPORT_SYMBOL(__DWC_ERROR);
EXPORT_SYMBOL(DWC_EXCEPTION);

#ifdef DEBUG
EXPORT_SYMBOL(__DWC_DEBUG);
#endif

EXPORT_SYMBOL(__DWC_DMA_ALLOC);
EXPORT_SYMBOL(__DWC_DMA_ALLOC_ATOMIC);
EXPORT_SYMBOL(__DWC_DMA_FREE);
EXPORT_SYMBOL(__DWC_ALLOC);
EXPORT_SYMBOL(__DWC_ALLOC_ATOMIC);
EXPORT_SYMBOL(__DWC_FREE);

#ifdef DWC_CRYPTOLIB
EXPORT_SYMBOL(DWC_RANDOM_BYTES);
EXPORT_SYMBOL(DWC_AES_CBC);
EXPORT_SYMBOL(DWC_SHA256);
EXPORT_SYMBOL(DWC_HMAC_SHA256);
#endif

EXPORT_SYMBOL(DWC_CPU_TO_LE32);
EXPORT_SYMBOL(DWC_CPU_TO_BE32);
EXPORT_SYMBOL(DWC_LE32_TO_CPU);
EXPORT_SYMBOL(DWC_BE32_TO_CPU);
EXPORT_SYMBOL(DWC_CPU_TO_LE16);
EXPORT_SYMBOL(DWC_CPU_TO_BE16);
EXPORT_SYMBOL(DWC_LE16_TO_CPU);
EXPORT_SYMBOL(DWC_BE16_TO_CPU);
EXPORT_SYMBOL(DWC_READ_REG32);
EXPORT_SYMBOL(DWC_WRITE_REG32);
EXPORT_SYMBOL(DWC_MODIFY_REG32);

#if 0
EXPORT_SYMBOL(DWC_READ_REG64);
EXPORT_SYMBOL(DWC_WRITE_REG64);
EXPORT_SYMBOL(DWC_MODIFY_REG64);
#endif

EXPORT_SYMBOL(DWC_SPINLOCK_ALLOC);
EXPORT_SYMBOL(DWC_SPINLOCK_FREE);
EXPORT_SYMBOL(DWC_SPINLOCK);
EXPORT_SYMBOL(DWC_SPINUNLOCK);
EXPORT_SYMBOL(DWC_SPINLOCK_IRQSAVE);
EXPORT_SYMBOL(DWC_SPINUNLOCK_IRQRESTORE);
EXPORT_SYMBOL(DWC_MUTEX_ALLOC);

#if (!defined(DWC_LINUX) || !defined(CONFIG_DEBUG_MUTEXES))
EXPORT_SYMBOL(DWC_MUTEX_FREE);
#endif

EXPORT_SYMBOL(DWC_MUTEX_LOCK);
EXPORT_SYMBOL(DWC_MUTEX_TRYLOCK);
EXPORT_SYMBOL(DWC_MUTEX_UNLOCK);
EXPORT_SYMBOL(DWC_UDELAY);
EXPORT_SYMBOL(DWC_MDELAY);
EXPORT_SYMBOL(DWC_MSLEEP);
EXPORT_SYMBOL(DWC_TIME);
EXPORT_SYMBOL(DWC_TIMER_ALLOC);
EXPORT_SYMBOL(DWC_TIMER_FREE);
EXPORT_SYMBOL(DWC_TIMER_SCHEDULE);
EXPORT_SYMBOL(DWC_TIMER_CANCEL);
EXPORT_SYMBOL(DWC_WAITQ_ALLOC);
EXPORT_SYMBOL(DWC_WAITQ_FREE);
EXPORT_SYMBOL(DWC_WAITQ_WAIT);
EXPORT_SYMBOL(DWC_WAITQ_WAIT_TIMEOUT);
EXPORT_SYMBOL(DWC_WAITQ_TRIGGER);
EXPORT_SYMBOL(DWC_WAITQ_ABORT);
EXPORT_SYMBOL(DWC_THREAD_RUN);
EXPORT_SYMBOL(DWC_THREAD_STOP);
EXPORT_SYMBOL(DWC_THREAD_SHOULD_STOP);
EXPORT_SYMBOL(DWC_TASK_ALLOC);
EXPORT_SYMBOL(DWC_TASK_FREE);
EXPORT_SYMBOL(DWC_TASK_SCHEDULE);
EXPORT_SYMBOL(DWC_WORKQ_WAIT_WORK_DONE);
EXPORT_SYMBOL(DWC_WORKQ_ALLOC);
EXPORT_SYMBOL(DWC_WORKQ_FREE);
EXPORT_SYMBOL(DWC_WORKQ_SCHEDULE);
EXPORT_SYMBOL(DWC_WORKQ_SCHEDULE_DELAYED);
EXPORT_SYMBOL(DWC_WORKQ_PENDING);

static int dwc_common_port_init_module(void)
{
	int result = 0;

	printk(KERN_DEBUG "Module dwc_common_port init\n");

#ifdef DWC_DEBUG_MEMORY
	result = dwc_memory_debug_start(NULL);
	if (result) {
		printk(KERN_ERR
		       "dwc_memory_debug_start() failed with error %d\n",
		       result);
		return result;
	}
#endif

#ifdef DWC_NOTIFYLIB
	result = dwc_alloc_notification_manager(NULL, NULL);
	if (result) {
		printk(KERN_ERR
		       "dwc_alloc_notification_manager() failed with error %d\n",
		       result);
		return result;
	}
#endif
	return result;
}

static void dwc_common_port_exit_module(void)
{
	printk(KERN_DEBUG "Module dwc_common_port exit\n");

#ifdef DWC_NOTIFYLIB
	dwc_free_notification_manager();
#endif

#ifdef DWC_DEBUG_MEMORY
	dwc_memory_debug_stop();
#endif
}

module_init(dwc_common_port_init_module);
module_exit(dwc_common_port_exit_module);

MODULE_DESCRIPTION("DWC Common Library - Portable version");
MODULE_AUTHOR("Synopsys Inc.");
MODULE_LICENSE ("GPL");

#endif	/* DWC_LIBMODULE */
