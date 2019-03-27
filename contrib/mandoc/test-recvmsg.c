#include <sys/socket.h>
#include <stddef.h>

int
main(void)
{
	return recvmsg(-1, NULL, 0) != -1;
}
