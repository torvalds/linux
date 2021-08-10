/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_DEBUGFS_HELPER_H_
#define _KBASE_DEBUGFS_HELPER_H_

/**
 * typedef kbase_debugfs_helper_set_attr_fn - Type of function to set an
 *                                            attribute value from an array
 *
 * @array: Address of an object that can be accessed like an array.
 * @index: An element index. The valid range depends on the use-case.
 * @value: Attribute value to be set.
 */
typedef void kbase_debugfs_helper_set_attr_fn(void *array, size_t index,
					      size_t value);

/**
 * kbase_debugfs_helper_set_attr_from_string - Parse a string to reconfigure an
 *                                             array
 *
 * The given function is called once for each attribute value found in the
 * input string. It is not an error if the string specifies fewer attribute
 * values than the specified number of array elements.
 *
 * The number base of each attribute value is detected automatically
 * according to the standard rules (e.g. prefix "0x" for hexadecimal).
 * Attribute values are separated by one or more space characters.
 * Additional leading and trailing spaces are ignored.
 *
 * @buf:         Input string to parse. Must be nul-terminated!
 * @array:       Address of an object that can be accessed like an array.
 * @nelems:      Number of elements in the array.
 * @set_attr_fn: Function to be called back for each array element.
 *
 * Return: 0 if success, negative error code otherwise.
 */
int kbase_debugfs_helper_set_attr_from_string(
	const char *buf, void *array, size_t nelems,
	kbase_debugfs_helper_set_attr_fn *set_attr_fn);

/**
 * kbase_debugfs_string_validator - Validate a string to be written to a
 *                                  debugfs file for any incorrect formats
 *                                  or wrong values.
 *
 * This function is to be used before any writes to debugfs values are done
 * such that any strings with erroneous values (such as octal 09 or
 * hexadecimal 0xGH are fully ignored) - without this validation, any correct
 * values before the first incorrect one will still be entered into the
 * debugfs file. This essentially iterates the values through kstrtoul to see
 * if it is valid.
 *
 * It is largely similar to set_attr_from_string to iterate through the values
 * of the input string. This function also requires the input string to be
 * writable.
 *
 * @buf: Null-terminated string to validate.
 *
 * Return: 0 with no error, else -22 (the invalid return value of kstrtoul) if
 *         any value in the string was wrong or with an incorrect format.
 */
int kbase_debugfs_string_validator(char *const buf);

/**
 * typedef kbase_debugfs_helper_get_attr_fn - Type of function to get an
 *                                            attribute value from an array
 *
 * @array: Address of an object that can be accessed like an array.
 * @index: An element index. The valid range depends on the use-case.
 *
 * Return: Value of attribute.
 */
typedef size_t kbase_debugfs_helper_get_attr_fn(void *array, size_t index);

/**
 * kbase_debugfs_helper_get_attr_to_string - Construct a formatted string
 *                                           from elements in an array
 *
 * The given function is called once for each array element to get the
 * value of the attribute to be inspected. The attribute values are
 * written to the buffer as a formatted string of decimal numbers
 * separated by spaces and terminated by a linefeed.
 *
 * @buf:         Buffer in which to store the formatted output string.
 * @size:        The size of the buffer, in bytes.
 * @array:       Address of an object that can be accessed like an array.
 * @nelems:      Number of elements in the array.
 * @get_attr_fn: Function to be called back for each array element.
 *
 * Return: Number of characters written excluding the nul terminator.
 */
ssize_t kbase_debugfs_helper_get_attr_to_string(
	char *buf, size_t size, void *array, size_t nelems,
	kbase_debugfs_helper_get_attr_fn *get_attr_fn);

/**
 * kbase_debugfs_helper_seq_read - Implements reads from a virtual file for an
 *                                 array
 *
 * The virtual file must have been opened by calling single_open and passing
 * the address of an object that can be accessed like an array.
 *
 * The given function is called once for each array element to get the
 * value of the attribute to be inspected. The attribute values are
 * written to the buffer as a formatted string of decimal numbers
 * separated by spaces and terminated by a linefeed.
 *
 * @sfile:       A virtual file previously opened by calling single_open.
 * @nelems:      Number of elements in the array.
 * @get_attr_fn: Function to be called back for each array element.
 *
 * Return: 0 if success, negative error code otherwise.
 */
int kbase_debugfs_helper_seq_read(
	struct seq_file *sfile, size_t nelems,
	kbase_debugfs_helper_get_attr_fn *get_attr_fn);

/**
 * kbase_debugfs_helper_seq_write - Implements writes to a virtual file for an
 *                                  array
 *
 * The virtual file must have been opened by calling single_open and passing
 * the address of an object that can be accessed like an array.
 *
 * The given function is called once for each attribute value found in the
 * data written to the virtual file. For further details, refer to the
 * description of set_attr_from_string.
 *
 * @file:        A virtual file previously opened by calling single_open.
 * @ubuf:        Source address in user space.
 * @count:       Number of bytes written to the virtual file.
 * @nelems:      Number of elements in the array.
 * @set_attr_fn: Function to be called back for each array element.
 *
 * Return: 0 if success, negative error code otherwise.
 */
int kbase_debugfs_helper_seq_write(struct file *file,
	const char __user *ubuf, size_t count,
	size_t nelems,
	kbase_debugfs_helper_set_attr_fn *set_attr_fn);

#endif  /*_KBASE_DEBUGFS_HELPER_H_ */

