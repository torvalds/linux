#include <stddef.h>
#include <sys/socket.h>

int
main(void)
{
	struct msghdr	 msg;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	return CMSG_FIRSTHDR(&msg) != NULL;
}
