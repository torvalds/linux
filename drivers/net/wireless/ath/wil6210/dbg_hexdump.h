#ifndef WIL_DBG_HEXDUMP_H_
#define WIL_DBG_HEXDUMP_H_

#include <linux/printk.h>
#include <linux/dynamic_debug.h>

#if defined(CONFIG_DYNAMIC_DEBUG)
#define wil_print_hex_dump_debug(prefix_str, prefix_type, rowsize,	\
				 groupsize, buf, len, ascii)		\
	dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			     groupsize, buf, len, ascii)

#else /* defined(CONFIG_DYNAMIC_DEBUG) */
#define wil_print_hex_dump_debug(prefix_str, prefix_type, rowsize,	\
				 groupsize, buf, len, ascii)		\
	print_hex_dump(KERN_DEBUG, prefix_str, prefix_type, rowsize,	\
		       groupsize, buf, len, ascii)
#endif /* defined(CONFIG_DYNAMIC_DEBUG) */

#endif /* WIL_DBG_HEXDUMP_H_ */
