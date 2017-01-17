/*
 * Message printing with message catalog prefixes.
 *
 * Copyright IBM Corp. 2012
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/jhash.h>
#include <linux/device.h>

static inline u32 __printk_jhash(const void *key, u32 length)
{
	u32 a, b, c, len;
	const u8 *k;
	u8 zk[12];

	a = b = 0x9e3779b9;
	c = 0;
	for (len = length + 12, k = key; len >= 12; len -= 12, k += 12) {
		if (len >= 24) {
			a += k[0] | k[1] << 8 | k[2] << 16 | k[3] << 24;
			b += k[4] | k[5] << 8 | k[6] << 16 | k[7] << 24;
			c += k[8] | k[9] << 8 | k[10] << 16 | k[11] << 24;
		} else {
			memset(zk, 0, 12);
			memcpy(zk, k, len - 12);
			a += zk[0] | zk[1] << 8 | zk[2] << 16 | zk[3] << 24;
			b += zk[4] | zk[5] << 8 | zk[6] << 16 | zk[7] << 24;
			c += (u32) zk[8] << 8;
			c += (u32) zk[9] << 16;
			c += (u32) zk[10] << 24;
			c += length;
		}
		a -= b + c; a ^= (c>>13);
		b -= a + c; b ^= (a<<8);
		c -= a + b; c ^= (b>>13);
		a -= b + c; a ^= (c>>12);
		b -= a + c; b ^= (a<<16);
		c -= a + b; c ^= (b>>5);
		a -= b + c; a ^= (c>>3);
		b -= a + c; b ^= (a<<10);
		c -= a + b; c ^= (b>>15);
	}
	return c;
}

/**
 * __jhash_string - calculate the six digit jhash of a string
 * @str: string to calculate the jhash
 */
unsigned long long __jhash_string(const char *str)
{
	return __printk_jhash(str, strlen(str)) & 0xffffff;
}
EXPORT_SYMBOL(__jhash_string);

static int __dev_printk_hash(const char *level, const struct device *dev,
			     struct va_format *vaf)
{
	if (!dev)
		return printk("%s(NULL device *): %pV", level, vaf);

	return printk("%s%s.%06x: %pV", level, dev_driver_string(dev),
		      __printk_jhash(vaf->fmt, strlen(vaf->fmt)) & 0xffffff,
		      vaf);
}

int dev_printk_hash(const char *level, const struct device *dev,
		    const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int r;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	r = __dev_printk_hash(level, dev, &vaf);
	va_end(args);

	return r;
}
EXPORT_SYMBOL(dev_printk_hash);

#define define_dev_printk_hash_level(func, kern_level)		\
int func(const struct device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf;					\
	va_list args;						\
	int r;							\
								\
	va_start(args, fmt);					\
								\
	vaf.fmt = fmt;						\
	vaf.va = &args;						\
								\
	r = __dev_printk_hash(kern_level, dev, &vaf);		\
	va_end(args);						\
								\
	return r;						\
}								\
EXPORT_SYMBOL(func);

define_dev_printk_hash_level(dev_emerg_hash, KERN_EMERG);
define_dev_printk_hash_level(dev_alert_hash, KERN_ALERT);
define_dev_printk_hash_level(dev_crit_hash, KERN_CRIT);
define_dev_printk_hash_level(dev_err_hash, KERN_ERR);
define_dev_printk_hash_level(dev_warn_hash, KERN_WARNING);
define_dev_printk_hash_level(dev_notice_hash, KERN_NOTICE);
define_dev_printk_hash_level(_dev_info_hash, KERN_INFO);
