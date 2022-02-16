// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "mali_kbase_debugfs_helper.h"

/* Arbitrary maximum size to prevent user space allocating too much kernel
 * memory
 */
#define DEBUGFS_MEM_POOLS_MAX_WRITE_SIZE (256u)

/**
 * set_attr_from_string - Parse a string to set elements of an array
 *
 * @buf:         Input string to parse. Must be nul-terminated!
 * @array:       Address of an object that can be accessed like an array.
 * @nelems:      Number of elements in the array.
 * @set_attr_fn: Function to be called back for each array element.
 *
 * This is the core of the implementation of
 * kbase_debugfs_helper_set_attr_from_string. The only difference between the
 * two functions is that this one requires the input string to be writable.
 *
 * Return: 0 if success, negative error code otherwise.
 */
static int
set_attr_from_string(char *const buf, void *const array, size_t const nelems,
		     kbase_debugfs_helper_set_attr_fn * const set_attr_fn)
{
	size_t index, err = 0;
	char *ptr = buf;

	for (index = 0; index < nelems && *ptr; ++index) {
		unsigned long new_size;
		size_t len;
		char sep;

		/* Drop leading spaces */
		while (*ptr == ' ')
			ptr++;

		len = strcspn(ptr, "\n ");
		if (len == 0) {
			/* No more values (allow this) */
			break;
		}

		/* Substitute a nul terminator for a space character
		 * to make the substring valid for kstrtoul.
		 */
		sep = ptr[len];
		if (sep == ' ')
			ptr[len++] = '\0';

		err = kstrtoul(ptr, 0, &new_size);
		if (err)
			break;

		/* Skip the substring (including any premature nul terminator)
		 */
		ptr += len;

		set_attr_fn(array, index, new_size);
	}

	return err;
}

int kbase_debugfs_string_validator(char *const buf)
{
	size_t index;
	int err = 0;
	char *ptr = buf;

	for (index = 0; *ptr; ++index) {
		unsigned long test_number;
		size_t len;

		/* Drop leading spaces */
		while (*ptr == ' ')
			ptr++;

		/* Strings passed into the validator will be NULL terminated
		 * by nature, so here strcspn only needs to delimit by
		 * newlines, spaces and NULL terminator (delimited natively).
		 */
		len = strcspn(ptr, "\n ");
		if (len == 0) {
			/* No more values (allow this) */
			break;
		}

		/* Substitute a nul terminator for a space character to make
		 * the substring valid for kstrtoul, and then replace it back.
		 */
		if (ptr[len] == ' ') {
			ptr[len] = '\0';
			err = kstrtoul(ptr, 0, &test_number);
			ptr[len] = ' ';

			/* len should only be incremented if there is a valid
			 * number to follow - otherwise this will skip over
			 * the NULL terminator in cases with no ending newline
			 */
			len++;
		} else {
			/* This would occur at the last element before a space
			 * or a NULL terminator.
			 */
			err = kstrtoul(ptr, 0, &test_number);
		}

		if (err)
			break;
		/* Skip the substring (including any premature nul terminator)
		 */
		ptr += len;
	}
	return err;
}

int kbase_debugfs_helper_set_attr_from_string(
	const char *const buf, void *const array, size_t const nelems,
	kbase_debugfs_helper_set_attr_fn * const set_attr_fn)
{
	char *const wbuf = kstrdup(buf, GFP_KERNEL);
	int err = 0;

	if (!wbuf)
		return -ENOMEM;

	/* validate string before actually writing values */
	err = kbase_debugfs_string_validator(wbuf);
	if (err) {
		kfree(wbuf);
		return err;
	}

	err = set_attr_from_string(wbuf, array, nelems,
		set_attr_fn);

	kfree(wbuf);
	return err;
}

ssize_t kbase_debugfs_helper_get_attr_to_string(
	char *const buf, size_t const size, void *const array,
	size_t const nelems,
	kbase_debugfs_helper_get_attr_fn * const get_attr_fn)
{
	ssize_t total = 0;
	size_t index;

	for (index = 0; index < nelems; ++index) {
		const char *postfix = " ";

		if (index == (nelems-1))
			postfix = "\n";

		total += scnprintf(buf + total, size - total, "%zu%s",
				get_attr_fn(array, index), postfix);
	}

	return total;
}

int kbase_debugfs_helper_seq_write(
	struct file *const file, const char __user *const ubuf,
	size_t const count, size_t const nelems,
	kbase_debugfs_helper_set_attr_fn * const set_attr_fn)
{
	const struct seq_file *const sfile = file->private_data;
	void *const array = sfile->private;
	int err = 0;
	char *buf;

	if (WARN_ON(!array))
		return -EINVAL;

	if (WARN_ON(count > DEBUGFS_MEM_POOLS_MAX_WRITE_SIZE))
		return -EINVAL;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, count)) {
		kfree(buf);
		return -EFAULT;
	}

	buf[count] = '\0';

	/* validate string before actually writing values */
	err = kbase_debugfs_string_validator(buf);
	if (err) {
		kfree(buf);
		return err;
	}

	err = set_attr_from_string(buf,
		array, nelems, set_attr_fn);
	kfree(buf);

	return err;
}

int kbase_debugfs_helper_seq_read(
	struct seq_file * const sfile, size_t const nelems,
	kbase_debugfs_helper_get_attr_fn * const get_attr_fn)
{
	void *const array = sfile->private;
	size_t index;

	if (WARN_ON(!array))
		return -EINVAL;

	for (index = 0; index < nelems; ++index) {
		const char *postfix = " ";

		if (index == (nelems-1))
			postfix = "\n";

		seq_printf(sfile, "%zu%s", get_attr_fn(array, index), postfix);
	}
	return 0;
}
