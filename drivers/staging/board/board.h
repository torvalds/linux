#ifndef __BOARD_H__
#define __BOARD_H__
#include <linux/init.h>
#include <linux/of.h>

bool board_staging_dt_node_available(const struct resource *resource,
				     unsigned int num_resources);

#define board_staging(str, fn)			\
static int __init runtime_board_check(void)	\
{						\
	if (of_machine_is_compatible(str))	\
		fn();				\
						\
	return 0;				\
}						\
						\
late_initcall(runtime_board_check)

#endif /* __BOARD_H__ */
