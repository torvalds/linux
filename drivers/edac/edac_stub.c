/*
 * common EDAC components that must be in kernel
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */
#include <linux/module.h>
#include <linux/edac.h>
#include <asm/atomic.h>
#include <asm/edac.h>

int edac_op_state = EDAC_OPSTATE_INVAL;
EXPORT_SYMBOL(edac_op_state);

atomic_t edac_handlers = ATOMIC_INIT(0);
EXPORT_SYMBOL(edac_handlers);

atomic_t edac_err_assert = ATOMIC_INIT(0);
EXPORT_SYMBOL(edac_err_assert);

inline int edac_handler_set(void)
{
	if (edac_op_state == EDAC_OPSTATE_POLL)
		return 0;

	return atomic_read(&edac_handlers);
}
EXPORT_SYMBOL(edac_handler_set);

/*
 * handler for NMI type of interrupts to assert error
 */
inline void edac_atomic_assert_error(void)
{
	atomic_set(&edac_err_assert, 1);
}
EXPORT_SYMBOL(edac_atomic_assert_error);
