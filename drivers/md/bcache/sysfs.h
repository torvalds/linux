/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHE_SYSFS_H_
#define _BCACHE_SYSFS_H_

#define KTYPE(type)							\
struct kobj_type type ## _ktype = {					\
	.release	= type ## _release,				\
	.sysfs_ops	= &((const struct sysfs_ops) {			\
		.show	= type ## _show,				\
		.store	= type ## _store				\
	}),								\
	.default_groups	= type ## _groups				\
}

#define SHOW(fn)							\
static ssize_t fn ## _show(struct kobject *kobj, struct attribute *attr,\
			   char *buf)					\

#define STORE(fn)							\
static ssize_t fn ## _store(struct kobject *kobj, struct attribute *attr,\
			    const char *buf, size_t size)		\

#define SHOW_LOCKED(fn)							\
SHOW(fn)								\
{									\
	ssize_t ret;							\
	mutex_lock(&bch_register_lock);					\
	ret = __ ## fn ## _show(kobj, attr, buf);			\
	mutex_unlock(&bch_register_lock);				\
	return ret;							\
}

#define STORE_LOCKED(fn)						\
STORE(fn)								\
{									\
	ssize_t ret;							\
	mutex_lock(&bch_register_lock);					\
	ret = __ ## fn ## _store(kobj, attr, buf, size);		\
	mutex_unlock(&bch_register_lock);				\
	return ret;							\
}

#define __sysfs_attribute(_name, _mode)					\
	static struct attribute sysfs_##_name =				\
		{ .name = #_name, .mode = _mode }

#define write_attribute(n)	__sysfs_attribute(n, 0200)
#define read_attribute(n)	__sysfs_attribute(n, 0444)
#define rw_attribute(n)		__sysfs_attribute(n, 0644)

#define sysfs_printf(file, fmt, ...)					\
do {									\
	if (attr == &sysfs_ ## file)					\
		return sysfs_emit(buf, fmt "\n", __VA_ARGS__);	\
} while (0)

#define sysfs_print(file, var)						\
do {									\
	if (attr == &sysfs_ ## file)					\
		return sysfs_emit(buf,						\
				__builtin_types_compatible_p(typeof(var), int)		\
					 ? "%i\n" :				\
				__builtin_types_compatible_p(typeof(var), unsigned int)	\
					 ? "%u\n" :				\
				__builtin_types_compatible_p(typeof(var), long)		\
					 ? "%li\n" :			\
				__builtin_types_compatible_p(typeof(var), unsigned long)\
					 ? "%lu\n" :			\
				__builtin_types_compatible_p(typeof(var), int64_t)	\
					 ? "%lli\n" :			\
				__builtin_types_compatible_p(typeof(var), uint64_t)	\
					 ? "%llu\n" :			\
				__builtin_types_compatible_p(typeof(var), const char *)	\
					 ? "%s\n" : "%i\n", var);	\
} while (0)

#define sysfs_hprint(file, val)						\
do {									\
	if (attr == &sysfs_ ## file) {					\
		ssize_t ret = bch_hprint(buf, val);			\
		strcat(buf, "\n");					\
		return ret + 1;						\
	}								\
} while (0)

#define var_printf(_var, fmt)	sysfs_printf(_var, fmt, var(_var))
#define var_print(_var)		sysfs_print(_var, var(_var))
#define var_hprint(_var)	sysfs_hprint(_var, var(_var))

#define sysfs_strtoul(file, var)					\
do {									\
	if (attr == &sysfs_ ## file)					\
		return strtoul_safe(buf, var) ?: (ssize_t) size;	\
} while (0)

#define sysfs_strtoul_bool(file, var)					\
do {									\
	if (attr == &sysfs_ ## file) {					\
		unsigned long v = strtoul_or_return(buf);		\
									\
		var = v ? 1 : 0;					\
		return size;						\
	}								\
} while (0)

#define sysfs_strtoul_clamp(file, var, min, max)			\
do {									\
	if (attr == &sysfs_ ## file) {					\
		unsigned long v = 0;					\
		ssize_t ret;						\
		ret = strtoul_safe_clamp(buf, v, min, max);		\
		if (!ret) {						\
			var = v;					\
			return size;					\
		}							\
		return ret;						\
	}								\
} while (0)

#define strtoul_or_return(cp)						\
({									\
	unsigned long _v;						\
	int _r = kstrtoul(cp, 10, &_v);					\
	if (_r)								\
		return _r;						\
	_v;								\
})

#define strtoi_h_or_return(cp, v)					\
do {									\
	int _r = strtoi_h(cp, &v);					\
	if (_r)								\
		return _r;						\
} while (0)

#define sysfs_hatoi(file, var)						\
do {									\
	if (attr == &sysfs_ ## file)					\
		return strtoi_h(buf, &var) ?: (ssize_t) size;		\
} while (0)

#endif  /* _BCACHE_SYSFS_H_ */
