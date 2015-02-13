/*
 * DHD logging module for internal debug
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_sdio.c 281456 2011-09-02 01:49:45Z $
 */

#include <typedefs.h>
#include <osl.h>

#include <proto/ethernet.h>
#include <proto/802.1d.h>
#include <proto/802.11.h>

#include <linux/inet.h>

void dhd_blog(char *cp, int size)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	static struct socket * _udpSocket = NULL;
	struct sockaddr_in _saAddr;
	struct iovec iov;
	struct msghdr msg;
	if (sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &_udpSocket) >= 0)
	{

		{
			memset(&_saAddr, 0, sizeof(_saAddr));
			_saAddr.sin_family      = AF_INET;
			_saAddr.sin_port        = htons(7651);
			_saAddr.sin_addr.s_addr = in_aton("10.19.74.43");

			iov.iov_base = cp;
			iov.iov_len = size;

			msg.msg_name = &_saAddr;
			msg.msg_namelen = sizeof(struct sockaddr_in);
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
			msg.msg_flags = 0;

			{
				mm_segment_t fs = get_fs();
				set_fs(get_ds());

				sock_sendmsg(_udpSocket, &msg, size);

				set_fs(fs);
			}
		}

		sock_release(_udpSocket);
	}
#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
}
