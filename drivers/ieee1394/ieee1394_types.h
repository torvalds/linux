#ifndef _IEEE1394_TYPES_H
#define _IEEE1394_TYPES_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/byteorder.h>

typedef u32 quadlet_t;
typedef u64 octlet_t;
typedef u16 nodeid_t;

typedef u8  byte_t;
typedef u64 nodeaddr_t;
typedef u16 arm_length_t;

#define BUS_MASK  0xffc0
#define BUS_SHIFT 6
#define NODE_MASK 0x003f
#define LOCAL_BUS 0xffc0
#define ALL_NODES 0x003f

#define NODEID_TO_BUS(nodeid)	((nodeid & BUS_MASK) >> BUS_SHIFT)
#define NODEID_TO_NODE(nodeid)	(nodeid & NODE_MASK)

/* Can be used to consistently print a node/bus ID. */
#define NODE_BUS_FMT		"%d-%02d:%04d"
#define NODE_BUS_ARGS(__host, __nodeid)	\
	__host->id, NODEID_TO_NODE(__nodeid), NODEID_TO_BUS(__nodeid)

#define HPSB_PRINT(level, fmt, args...) \
	printk(level "ieee1394: " fmt "\n" , ## args)

#define HPSB_DEBUG(fmt, args...)	HPSB_PRINT(KERN_DEBUG, fmt , ## args)
#define HPSB_INFO(fmt, args...)		HPSB_PRINT(KERN_INFO, fmt , ## args)
#define HPSB_NOTICE(fmt, args...)	HPSB_PRINT(KERN_NOTICE, fmt , ## args)
#define HPSB_WARN(fmt, args...)		HPSB_PRINT(KERN_WARNING, fmt , ## args)
#define HPSB_ERR(fmt, args...)		HPSB_PRINT(KERN_ERR, fmt , ## args)

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define HPSB_VERBOSE(fmt, args...)	HPSB_PRINT(KERN_DEBUG, fmt , ## args)
#define HPSB_DEBUG_TLABELS
#else
#define HPSB_VERBOSE(fmt, args...)	do {} while (0)
#endif

#ifdef __BIG_ENDIAN

static inline void *memcpy_le32(u32 *dest, const u32 *__src, size_t count)
{
	void *tmp = dest;
	u32 *src = (u32 *)__src;

	count /= 4;
	while (count--)
		*dest++ = swab32p(src++);
	return tmp;
}

#else

static __inline__ void *memcpy_le32(u32 *dest, const u32 *src, size_t count)
{
	return memcpy(dest, src, count);
}

#endif /* __BIG_ENDIAN */

#endif /* _IEEE1394_TYPES_H */
