/*
 * User address space access functions.
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/uaccess.h>
#include <linux/export.h>

#include <asm/tlbflush.h>

/**
 * copy_from_user_nmi - NMI safe copy from user
 * @to:		Pointer to the destination buffer
 * @from:	Pointer to a user space address of the current task
 * @n:		Number of bytes to copy
 *
 * Returns: The number of not copied bytes. 0 is success, i.e. all bytes copied
 *
 * Contrary to other copy_from_user() variants this function can be called
 * from NMI context. Despite the name it is not restricted to be called
 * from NMI context. It is safe to be called from any other context as
 * well. It disables pagefaults across the copy which means a fault will
 * abort the copy.
 *
 * For NMI context invocations this relies on the nested NMI work to allow
 * atomic faults from the NMI path; the nested NMI paths are careful to
 * preserve CR2.
 */
unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n)
{
	unsigned long ret;

	if (__range_not_ok(from, n, TASK_SIZE))
		return n;

	if (!nmi_uaccess_okay())
		return n;

	/*
	 * Even though this function is typically called from NMI/IRQ context
	 * disable pagefaults so that its behaviour is consistent even when
	 * called from other contexts.
	 */
	pagefault_disable();
	ret = __copy_from_user_inatomic(to, from, n);
	pagefault_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(copy_from_user_nmi);
