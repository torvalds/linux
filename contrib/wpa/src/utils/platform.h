#ifndef PLATFORM_H
#define PLATFORM_H

#include "includes.h"
#include "common.h"

#define le16_to_cpu		le_to_host16
#define le32_to_cpu		le_to_host32

#define get_unaligned(p)					\
({								\
	struct packed_dummy_struct {				\
		typeof(*(p)) __val;				\
	} __attribute__((packed)) *__ptr = (void *) (p);	\
								\
	__ptr->__val;						\
})
#define get_unaligned_le16(p)	le16_to_cpu(get_unaligned((le16 *)(p)))
#define get_unaligned_le32(p)	le32_to_cpu(get_unaligned((le32 *)(p)))

#endif /* PLATFORM_H */
