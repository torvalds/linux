/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KERNEL_UTF_HELPERS_H_
#define _KERNEL_UTF_HELPERS_H_

/* kutf_helpers.h
 * Test helper functions for the kernel UTF test infrastructure.
 *
 * These functions provide methods for enqueuing/dequeuing lines of text sent
 * by user space. They are used to implement the transfer of "userdata" from
 * user space to kernel.
 */

#include <kutf/kutf_suite.h>

/**
 * kutf_helper_input_dequeue() - Dequeue a line sent by user space
 * @context:    KUTF context
 * @str_size:   Pointer to an integer to receive the size of the string
 *
 * If no line is available then this function will wait (interruptibly) until
 * a line is available.
 *
 * Return: The line dequeued, ERR_PTR(-EINTR) if interrupted or NULL on end
 * of data.
 */
char *kutf_helper_input_dequeue(struct kutf_context *context, size_t *str_size);

/**
 * kutf_helper_input_enqueue() - Enqueue a line sent by user space
 * @context:   KUTF context
 * @str:       The user space address of the line
 * @size:      The length in bytes of the string
 *
 * This function will use copy_from_user to copy the string out of user space.
 * The string need not be NULL-terminated (@size should not include the NULL
 * termination).
 *
 * As a special case @str==NULL and @size==0 is valid to mark the end of input,
 * but callers should use kutf_helper_input_enqueue_end_of_data() instead.
 *
 * Return: 0 on success, -EFAULT if the line cannot be copied from user space,
 * -ENOMEM if out of memory.
 */
int kutf_helper_input_enqueue(struct kutf_context *context,
		const char __user *str, size_t size);

/**
 * kutf_helper_input_enqueue_end_of_data() - Signal no more data is to be sent
 * @context:    KUTF context
 *
 * After this function has been called, kutf_helper_input_dequeue() will always
 * return NULL.
 */
void kutf_helper_input_enqueue_end_of_data(struct kutf_context *context);

#endif	/* _KERNEL_UTF_HELPERS_H_ */
