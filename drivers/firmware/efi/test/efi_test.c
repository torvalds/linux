// SPDX-License-Identifier: GPL-2.0+
/*
 * EFI Test Driver for Runtime Services
 *
 * Copyright(C) 2012-2016 Canonical Ltd.
 *
 * This driver exports EFI runtime services interfaces into userspace, which
 * allow to use and test UEFI runtime services provided by firmware.
 *
 */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/efi.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "efi_test.h"

MODULE_AUTHOR("Ivan Hu <ivan.hu@canonical.com>");
MODULE_DESCRIPTION("EFI Test Driver");
MODULE_LICENSE("GPL");

/*
 * Count the bytes in 'str', including the terminating NULL.
 *
 * Note this function returns the number of *bytes*, not the number of
 * ucs2 characters.
 */
static inline size_t user_ucs2_strsize(efi_char16_t  __user *str)
{
	efi_char16_t *s = str, c;
	size_t len;

	if (!str)
		return 0;

	/* Include terminating NULL */
	len = sizeof(efi_char16_t);

	if (get_user(c, s++)) {
		/* Can't read userspace memory for size */
		return 0;
	}

	while (c != 0) {
		if (get_user(c, s++)) {
			/* Can't read userspace memory for size */
			return 0;
		}
		len += sizeof(efi_char16_t);
	}
	return len;
}

/*
 * Allocate a buffer and copy a ucs2 string from user space into it.
 */
static inline int
copy_ucs2_from_user_len(efi_char16_t **dst, efi_char16_t __user *src,
			size_t len)
{
	efi_char16_t *buf;

	if (!src) {
		*dst = NULL;
		return 0;
	}

	buf = memdup_user(src, len);
	if (IS_ERR(buf)) {
		*dst = NULL;
		return PTR_ERR(buf);
	}
	*dst = buf;

	return 0;
}

/*
 * Count the bytes in 'str', including the terminating NULL.
 *
 * Just a wrap for user_ucs2_strsize
 */
static inline int
get_ucs2_strsize_from_user(efi_char16_t __user *src, size_t *len)
{
	*len = user_ucs2_strsize(src);
	if (*len == 0)
		return -EFAULT;

	return 0;
}

/*
 * Calculate the required buffer allocation size and copy a ucs2 string
 * from user space into it.
 *
 * This function differs from copy_ucs2_from_user_len() because it
 * calculates the size of the buffer to allocate by taking the length of
 * the string 'src'.
 *
 * If a non-zero value is returned, the caller MUST NOT access 'dst'.
 *
 * It is the caller's responsibility to free 'dst'.
 */
static inline int
copy_ucs2_from_user(efi_char16_t **dst, efi_char16_t __user *src)
{
	size_t len;

	len = user_ucs2_strsize(src);
	if (len == 0)
		return -EFAULT;
	return copy_ucs2_from_user_len(dst, src, len);
}

/*
 * Copy a ucs2 string to a user buffer.
 *
 * This function is a simple wrapper around copy_to_user() that does
 * nothing if 'src' is NULL, which is useful for reducing the amount of
 * NULL checking the caller has to do.
 *
 * 'len' specifies the number of bytes to copy.
 */
static inline int
copy_ucs2_to_user_len(efi_char16_t __user *dst, efi_char16_t *src, size_t len)
{
	if (!src)
		return 0;

	return copy_to_user(dst, src, len);
}

static long efi_runtime_get_variable(unsigned long arg)
{
	struct efi_getvariable __user *getvariable_user;
	struct efi_getvariable getvariable;
	unsigned long datasize = 0, prev_datasize, *dz;
	efi_guid_t vendor_guid, *vd = NULL;
	efi_status_t status;
	efi_char16_t *name = NULL;
	u32 attr, *at;
	void *data = NULL;
	int rv = 0;

	getvariable_user = (struct efi_getvariable __user *)arg;

	if (copy_from_user(&getvariable, getvariable_user,
			   sizeof(getvariable)))
		return -EFAULT;
	if (getvariable.data_size &&
	    get_user(datasize, getvariable.data_size))
		return -EFAULT;
	if (getvariable.vendor_guid) {
		if (copy_from_user(&vendor_guid, getvariable.vendor_guid,
					sizeof(vendor_guid)))
			return -EFAULT;
		vd = &vendor_guid;
	}

	if (getvariable.variable_name) {
		rv = copy_ucs2_from_user(&name, getvariable.variable_name);
		if (rv)
			return rv;
	}

	at = getvariable.attributes ? &attr : NULL;
	dz = getvariable.data_size ? &datasize : NULL;

	if (getvariable.data_size && getvariable.data) {
		data = kmalloc(datasize, GFP_KERNEL);
		if (!data) {
			kfree(name);
			return -ENOMEM;
		}
	}

	prev_datasize = datasize;
	status = efi.get_variable(name, vd, at, dz, data);
	kfree(name);

	if (put_user(status, getvariable.status)) {
		rv = -EFAULT;
		goto out;
	}

	if (status != EFI_SUCCESS) {
		if (status == EFI_BUFFER_TOO_SMALL) {
			if (dz && put_user(datasize, getvariable.data_size)) {
				rv = -EFAULT;
				goto out;
			}
		}
		rv = -EINVAL;
		goto out;
	}

	if (prev_datasize < datasize) {
		rv = -EINVAL;
		goto out;
	}

	if (data) {
		if (copy_to_user(getvariable.data, data, datasize)) {
			rv = -EFAULT;
			goto out;
		}
	}

	if (at && put_user(attr, getvariable.attributes)) {
		rv = -EFAULT;
		goto out;
	}

	if (dz && put_user(datasize, getvariable.data_size))
		rv = -EFAULT;

out:
	kfree(data);
	return rv;

}

static long efi_runtime_set_variable(unsigned long arg)
{
	struct efi_setvariable __user *setvariable_user;
	struct efi_setvariable setvariable;
	efi_guid_t vendor_guid;
	efi_status_t status;
	efi_char16_t *name = NULL;
	void *data;
	int rv = 0;

	setvariable_user = (struct efi_setvariable __user *)arg;

	if (copy_from_user(&setvariable, setvariable_user, sizeof(setvariable)))
		return -EFAULT;
	if (copy_from_user(&vendor_guid, setvariable.vendor_guid,
				sizeof(vendor_guid)))
		return -EFAULT;

	if (setvariable.variable_name) {
		rv = copy_ucs2_from_user(&name, setvariable.variable_name);
		if (rv)
			return rv;
	}

	data = memdup_user(setvariable.data, setvariable.data_size);
	if (IS_ERR(data)) {
		kfree(name);
		return PTR_ERR(data);
	}

	status = efi.set_variable(name, &vendor_guid,
				setvariable.attributes,
				setvariable.data_size, data);

	if (put_user(status, setvariable.status)) {
		rv = -EFAULT;
		goto out;
	}

	rv = status == EFI_SUCCESS ? 0 : -EINVAL;

out:
	kfree(data);
	kfree(name);

	return rv;
}

static long efi_runtime_get_time(unsigned long arg)
{
	struct efi_gettime __user *gettime_user;
	struct efi_gettime  gettime;
	efi_status_t status;
	efi_time_cap_t cap;
	efi_time_t efi_time;

	gettime_user = (struct efi_gettime __user *)arg;
	if (copy_from_user(&gettime, gettime_user, sizeof(gettime)))
		return -EFAULT;

	status = efi.get_time(gettime.time ? &efi_time : NULL,
			      gettime.capabilities ? &cap : NULL);

	if (put_user(status, gettime.status))
		return -EFAULT;

	if (status != EFI_SUCCESS)
		return -EINVAL;

	if (gettime.capabilities) {
		efi_time_cap_t __user *cap_local;

		cap_local = (efi_time_cap_t *)gettime.capabilities;
		if (put_user(cap.resolution, &(cap_local->resolution)) ||
			put_user(cap.accuracy, &(cap_local->accuracy)) ||
			put_user(cap.sets_to_zero, &(cap_local->sets_to_zero)))
			return -EFAULT;
	}
	if (gettime.time) {
		if (copy_to_user(gettime.time, &efi_time, sizeof(efi_time_t)))
			return -EFAULT;
	}

	return 0;
}

static long efi_runtime_set_time(unsigned long arg)
{
	struct efi_settime __user *settime_user;
	struct efi_settime settime;
	efi_status_t status;
	efi_time_t efi_time;

	settime_user = (struct efi_settime __user *)arg;
	if (copy_from_user(&settime, settime_user, sizeof(settime)))
		return -EFAULT;
	if (copy_from_user(&efi_time, settime.time,
					sizeof(efi_time_t)))
		return -EFAULT;
	status = efi.set_time(&efi_time);

	if (put_user(status, settime.status))
		return -EFAULT;

	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static long efi_runtime_get_waketime(unsigned long arg)
{
	struct efi_getwakeuptime __user *getwakeuptime_user;
	struct efi_getwakeuptime getwakeuptime;
	efi_bool_t enabled, pending;
	efi_status_t status;
	efi_time_t efi_time;

	getwakeuptime_user = (struct efi_getwakeuptime __user *)arg;
	if (copy_from_user(&getwakeuptime, getwakeuptime_user,
				sizeof(getwakeuptime)))
		return -EFAULT;

	status = efi.get_wakeup_time(
		getwakeuptime.enabled ? (efi_bool_t *)&enabled : NULL,
		getwakeuptime.pending ? (efi_bool_t *)&pending : NULL,
		getwakeuptime.time ? &efi_time : NULL);

	if (put_user(status, getwakeuptime.status))
		return -EFAULT;

	if (status != EFI_SUCCESS)
		return -EINVAL;

	if (getwakeuptime.enabled && put_user(enabled,
						getwakeuptime.enabled))
		return -EFAULT;

	if (getwakeuptime.pending && put_user(pending,
						getwakeuptime.pending))
		return -EFAULT;

	if (getwakeuptime.time) {
		if (copy_to_user(getwakeuptime.time, &efi_time,
				sizeof(efi_time_t)))
			return -EFAULT;
	}

	return 0;
}

static long efi_runtime_set_waketime(unsigned long arg)
{
	struct efi_setwakeuptime __user *setwakeuptime_user;
	struct efi_setwakeuptime setwakeuptime;
	efi_bool_t enabled;
	efi_status_t status;
	efi_time_t efi_time;

	setwakeuptime_user = (struct efi_setwakeuptime __user *)arg;

	if (copy_from_user(&setwakeuptime, setwakeuptime_user,
				sizeof(setwakeuptime)))
		return -EFAULT;

	enabled = setwakeuptime.enabled;
	if (setwakeuptime.time) {
		if (copy_from_user(&efi_time, setwakeuptime.time,
					sizeof(efi_time_t)))
			return -EFAULT;

		status = efi.set_wakeup_time(enabled, &efi_time);
	} else
		status = efi.set_wakeup_time(enabled, NULL);

	if (put_user(status, setwakeuptime.status))
		return -EFAULT;

	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static long efi_runtime_get_nextvariablename(unsigned long arg)
{
	struct efi_getnextvariablename __user *getnextvariablename_user;
	struct efi_getnextvariablename getnextvariablename;
	unsigned long name_size, prev_name_size = 0, *ns = NULL;
	efi_status_t status;
	efi_guid_t *vd = NULL;
	efi_guid_t vendor_guid;
	efi_char16_t *name = NULL;
	int rv = 0;

	getnextvariablename_user = (struct efi_getnextvariablename __user *)arg;

	if (copy_from_user(&getnextvariablename, getnextvariablename_user,
			   sizeof(getnextvariablename)))
		return -EFAULT;

	if (getnextvariablename.variable_name_size) {
		if (get_user(name_size, getnextvariablename.variable_name_size))
			return -EFAULT;
		ns = &name_size;
		prev_name_size = name_size;
	}

	if (getnextvariablename.vendor_guid) {
		if (copy_from_user(&vendor_guid,
				getnextvariablename.vendor_guid,
				sizeof(vendor_guid)))
			return -EFAULT;
		vd = &vendor_guid;
	}

	if (getnextvariablename.variable_name) {
		size_t name_string_size = 0;

		rv = get_ucs2_strsize_from_user(
				getnextvariablename.variable_name,
				&name_string_size);
		if (rv)
			return rv;
		/*
		 * The name_size may be smaller than the real buffer size where
		 * variable name located in some use cases. The most typical
		 * case is passing a 0 to get the required buffer size for the
		 * 1st time call. So we need to copy the content from user
		 * space for at least the string size of variable name, or else
		 * the name passed to UEFI may not be terminated as we expected.
		 */
		rv = copy_ucs2_from_user_len(&name,
				getnextvariablename.variable_name,
				prev_name_size > name_string_size ?
				prev_name_size : name_string_size);
		if (rv)
			return rv;
	}

	status = efi.get_next_variable(ns, name, vd);

	if (put_user(status, getnextvariablename.status)) {
		rv = -EFAULT;
		goto out;
	}

	if (status != EFI_SUCCESS) {
		if (status == EFI_BUFFER_TOO_SMALL) {
			if (ns && put_user(*ns,
				getnextvariablename.variable_name_size)) {
				rv = -EFAULT;
				goto out;
			}
		}
		rv = -EINVAL;
		goto out;
	}

	if (name) {
		if (copy_ucs2_to_user_len(getnextvariablename.variable_name,
						name, prev_name_size)) {
			rv = -EFAULT;
			goto out;
		}
	}

	if (ns) {
		if (put_user(*ns, getnextvariablename.variable_name_size)) {
			rv = -EFAULT;
			goto out;
		}
	}

	if (vd) {
		if (copy_to_user(getnextvariablename.vendor_guid, vd,
							sizeof(efi_guid_t)))
			rv = -EFAULT;
	}

out:
	kfree(name);
	return rv;
}

static long efi_runtime_get_nexthighmonocount(unsigned long arg)
{
	struct efi_getnexthighmonotoniccount __user *getnexthighmonocount_user;
	struct efi_getnexthighmonotoniccount getnexthighmonocount;
	efi_status_t status;
	u32 count;

	getnexthighmonocount_user = (struct
			efi_getnexthighmonotoniccount __user *)arg;

	if (copy_from_user(&getnexthighmonocount,
			   getnexthighmonocount_user,
			   sizeof(getnexthighmonocount)))
		return -EFAULT;

	status = efi.get_next_high_mono_count(
		getnexthighmonocount.high_count ? &count : NULL);

	if (put_user(status, getnexthighmonocount.status))
		return -EFAULT;

	if (status != EFI_SUCCESS)
		return -EINVAL;

	if (getnexthighmonocount.high_count &&
	    put_user(count, getnexthighmonocount.high_count))
		return -EFAULT;

	return 0;
}

static long efi_runtime_reset_system(unsigned long arg)
{
	struct efi_resetsystem __user *resetsystem_user;
	struct efi_resetsystem resetsystem;
	void *data = NULL;

	resetsystem_user = (struct efi_resetsystem __user *)arg;
	if (copy_from_user(&resetsystem, resetsystem_user,
						sizeof(resetsystem)))
		return -EFAULT;
	if (resetsystem.data_size != 0) {
		data = memdup_user((void *)resetsystem.data,
						resetsystem.data_size);
		if (IS_ERR(data))
			return PTR_ERR(data);
	}

	efi.reset_system(resetsystem.reset_type, resetsystem.status,
				resetsystem.data_size, (efi_char16_t *)data);

	kfree(data);
	return 0;
}

static long efi_runtime_query_variableinfo(unsigned long arg)
{
	struct efi_queryvariableinfo __user *queryvariableinfo_user;
	struct efi_queryvariableinfo queryvariableinfo;
	efi_status_t status;
	u64 max_storage, remaining, max_size;

	queryvariableinfo_user = (struct efi_queryvariableinfo __user *)arg;

	if (copy_from_user(&queryvariableinfo, queryvariableinfo_user,
			   sizeof(queryvariableinfo)))
		return -EFAULT;

	status = efi.query_variable_info(queryvariableinfo.attributes,
					 &max_storage, &remaining, &max_size);

	if (put_user(status, queryvariableinfo.status))
		return -EFAULT;

	if (status != EFI_SUCCESS)
		return -EINVAL;

	if (put_user(max_storage,
		     queryvariableinfo.maximum_variable_storage_size))
		return -EFAULT;

	if (put_user(remaining,
		     queryvariableinfo.remaining_variable_storage_size))
		return -EFAULT;

	if (put_user(max_size, queryvariableinfo.maximum_variable_size))
		return -EFAULT;

	return 0;
}

static long efi_runtime_query_capsulecaps(unsigned long arg)
{
	struct efi_querycapsulecapabilities __user *qcaps_user;
	struct efi_querycapsulecapabilities qcaps;
	efi_capsule_header_t *capsules;
	efi_status_t status;
	u64 max_size;
	int i, reset_type;
	int rv = 0;

	qcaps_user = (struct efi_querycapsulecapabilities __user *)arg;

	if (copy_from_user(&qcaps, qcaps_user, sizeof(qcaps)))
		return -EFAULT;

	if (qcaps.capsule_count == ULONG_MAX)
		return -EINVAL;

	capsules = kcalloc(qcaps.capsule_count + 1,
			   sizeof(efi_capsule_header_t), GFP_KERNEL);
	if (!capsules)
		return -ENOMEM;

	for (i = 0; i < qcaps.capsule_count; i++) {
		efi_capsule_header_t *c;
		/*
		 * We cannot dereference qcaps.capsule_header_array directly to
		 * obtain the address of the capsule as it resides in the
		 * user space
		 */
		if (get_user(c, qcaps.capsule_header_array + i)) {
			rv = -EFAULT;
			goto out;
		}
		if (copy_from_user(&capsules[i], c,
				sizeof(efi_capsule_header_t))) {
			rv = -EFAULT;
			goto out;
		}
	}

	qcaps.capsule_header_array = &capsules;

	status = efi.query_capsule_caps((efi_capsule_header_t **)
					qcaps.capsule_header_array,
					qcaps.capsule_count,
					&max_size, &reset_type);

	if (put_user(status, qcaps.status)) {
		rv = -EFAULT;
		goto out;
	}

	if (status != EFI_SUCCESS) {
		rv = -EINVAL;
		goto out;
	}

	if (put_user(max_size, qcaps.maximum_capsule_size)) {
		rv = -EFAULT;
		goto out;
	}

	if (put_user(reset_type, qcaps.reset_type))
		rv = -EFAULT;

out:
	kfree(capsules);
	return rv;
}

static long efi_runtime_get_supported_mask(unsigned long arg)
{
	unsigned int __user *supported_mask;
	int rv = 0;

	supported_mask = (unsigned int *)arg;

	if (put_user(efi.runtime_supported_mask, supported_mask))
		rv = -EFAULT;

	return rv;
}

static long efi_test_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	switch (cmd) {
	case EFI_RUNTIME_GET_VARIABLE:
		return efi_runtime_get_variable(arg);

	case EFI_RUNTIME_SET_VARIABLE:
		return efi_runtime_set_variable(arg);

	case EFI_RUNTIME_GET_TIME:
		return efi_runtime_get_time(arg);

	case EFI_RUNTIME_SET_TIME:
		return efi_runtime_set_time(arg);

	case EFI_RUNTIME_GET_WAKETIME:
		return efi_runtime_get_waketime(arg);

	case EFI_RUNTIME_SET_WAKETIME:
		return efi_runtime_set_waketime(arg);

	case EFI_RUNTIME_GET_NEXTVARIABLENAME:
		return efi_runtime_get_nextvariablename(arg);

	case EFI_RUNTIME_GET_NEXTHIGHMONOTONICCOUNT:
		return efi_runtime_get_nexthighmonocount(arg);

	case EFI_RUNTIME_QUERY_VARIABLEINFO:
		return efi_runtime_query_variableinfo(arg);

	case EFI_RUNTIME_QUERY_CAPSULECAPABILITIES:
		return efi_runtime_query_capsulecaps(arg);

	case EFI_RUNTIME_RESET_SYSTEM:
		return efi_runtime_reset_system(arg);

	case EFI_RUNTIME_GET_SUPPORTED_MASK:
		return efi_runtime_get_supported_mask(arg);
	}

	return -ENOTTY;
}

static int efi_test_open(struct inode *inode, struct file *file)
{
	int ret = security_locked_down(LOCKDOWN_EFI_TEST);

	if (ret)
		return ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	/*
	 * nothing special to do here
	 * We do accept multiple open files at the same time as we
	 * synchronize on the per call operation.
	 */
	return 0;
}

static int efi_test_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	The various file operations we support.
 */
static const struct file_operations efi_test_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= efi_test_ioctl,
	.open		= efi_test_open,
	.release	= efi_test_close,
};

static struct miscdevice efi_test_dev = {
	MISC_DYNAMIC_MINOR,
	"efi_test",
	&efi_test_fops
};

static int __init efi_test_init(void)
{
	int ret;

	ret = misc_register(&efi_test_dev);
	if (ret) {
		pr_err("efi_test: can't misc_register on minor=%d\n",
			MISC_DYNAMIC_MINOR);
		return ret;
	}

	return 0;
}

static void __exit efi_test_exit(void)
{
	misc_deregister(&efi_test_dev);
}

module_init(efi_test_init);
module_exit(efi_test_exit);
