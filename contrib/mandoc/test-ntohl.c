#include <arpa/inet.h>

int
main(void)
{
	return htonl(ntohl(0x3a7d0cdb)) != 0x3a7d0cdb;
}
