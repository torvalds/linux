#ifndef WIL_DBG_HEXDUMP_H_
#define WIL_DBG_HEXDUMP_H_

#if defined(CONFIG_DYNAMIC_DEBUG)
#define wil_dynamic_hex_dump(prefix_str, prefix_type, rowsize,	\
			     groupsize, buf, len, ascii)	\
do {								\
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor,		\
		__builtin_constant_p(prefix_str) ? prefix_str : "hexdump");\
	if (unlikely(descriptor.flags & _DPRINTK_FLAGS_PRINT))	\
		print_hex_dump(KERN_DEBUG, prefix_str,		\
			       prefix_type, rowsize, groupsize,	\
			       buf, len, ascii);		\
} while (0)

#define wil_print_hex_dump_debug(prefix_str, prefix_type, rowsize,	\
				 groupsize, buf, len, ascii)		\
	wil_dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			     groupsize, buf, len, ascii)

#define print_hex_dump_bytes(prefix_str, prefix_type, buf, len)	\
	wil_dynamic_hex_dump(prefix_str, prefix_type, 16, 1, buf, len, true)
#else /* defined(CONFIG_DYNAMIC_DEBUG) */
#define wil_print_hex_dump_debug(prefix_str, prefix_type, rowsize,	\
				 groupsize, buf, len, ascii)		\
	print_hex_dump(KERN_DEBUG, prefix_str, prefix_type, rowsize,	\
		       groupsize, buf, len, ascii)
#endif /* defined(CONFIG_DYNAMIC_DEBUG) */

#endif /* WIL_DBG_HEXDUMP_H_ */
