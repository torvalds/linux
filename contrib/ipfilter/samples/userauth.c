/*	$FreeBSD$	*/

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <net/if.h>
#include "ip_compat.h"
#include "ip_fil.h"
#include "ip_auth.h"

extern	int	errno;

main()
{
	struct frauth fra;
	struct frauth *frap = &fra;
	fr_info_t *fin = &fra.fra_info;
	fr_ip_t	*fi = &fin->fin_fi;
	char yn[16];
	int fd;

	fd = open(IPL_NAME, O_RDWR);
	fra.fra_len = 0;
	fra.fra_buf = NULL;
	while (ioctl(fd, SIOCAUTHW, &frap) == 0) {
		if (fra.fra_info.fin_out)
			fra.fra_pass = FR_OUTQUE;
		else
			fra.fra_pass = FR_INQUE;

		printf("%s ", inet_ntoa(fi->fi_src));
		if (fi->fi_flx & FI_TCPUDP)
			printf("port %d ", fin->fin_data[0]);
		printf("-> %s ", inet_ntoa(fi->fi_dst));
		if (fi->fi_flx & FI_TCPUDP)
			printf("port %d ", fin->fin_data[1]);
		printf("\n");
		printf("Allow packet through ? [y/n]");
		fflush(stdout);
		if (!fgets(yn, sizeof(yn), stdin))
			break;
		fflush(stdin);
		if (yn[0] == 'n' || yn[0] == 'N')
			fra.fra_pass |= FR_BLOCK;
		else if (yn[0] == 'y' || yn[0] == 'Y') {
			fra.fra_pass |= FR_PASS;
			if (fra.fra_info.fin_fi.fi_flx & FI_TCPUDP)
				fra.fra_pass |= FR_KEEPSTATE;
		} else
			fra.fra_pass |= FR_NOMATCH;
		printf("answer = %c (%x), id %d idx %d\n", yn[0],
			fra.fra_pass, fra.fra_info.fin_id, fra.fra_index);
		if (ioctl(fd, SIOCAUTHR, &frap) != 0)
			perror("SIOCAUTHR");
	}
	fprintf(stderr, "errno=%d \n", errno);
	perror("frauth-SIOCAUTHW");
}
