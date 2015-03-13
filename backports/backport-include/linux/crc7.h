#ifndef _BACKPORT_LINUX_CRC7_H
#define _BACKPORT_LINUX_CRC7_H
#include_next <linux/crc7.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
#define crc7_be LINUX_BACKPORT(crc7_be)
static inline u8 crc7_be(u8 crc, const u8 *buffer, size_t len)
{
	return crc7(crc, buffer, len) << 1;
}
#endif /* < 3.16 */

#endif /* _BACKPORT_LINUX_CRC7_H */
