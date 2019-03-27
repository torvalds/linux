#ifdef SYS_ENDIAN
#include <sys/endian.h>
#else
#include <endian.h>
#endif

int
main(void)
{
	return htobe32(be32toh(0x3a7d0cdb)) != 0x3a7d0cdb;
}
