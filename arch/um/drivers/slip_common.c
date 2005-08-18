#include <string.h>
#include "slip_common.h"
#include "net_user.h"

int slip_proto_read(int fd, void *buf, int len, struct slip_proto *slip)
{
	int i, n, size, start;

	if(slip->more > 0){
		i = 0;
		while(i < slip->more){
			size = slip_unesc(slip->ibuf[i++], slip->ibuf,
					  &slip->pos, &slip->esc);
			if(size){
				memcpy(buf, slip->ibuf, size);
				memmove(slip->ibuf, &slip->ibuf[i],
					slip->more - i);
				slip->more = slip->more - i;
				return size;
			}
		}
		slip->more = 0;
	}

	n = net_read(fd, &slip->ibuf[slip->pos],
		     sizeof(slip->ibuf) - slip->pos);
	if(n <= 0)
		return n;

	start = slip->pos;
	for(i = 0; i < n; i++){
		size = slip_unesc(slip->ibuf[start + i], slip->ibuf,&slip->pos,
				  &slip->esc);
		if(size){
			memcpy(buf, slip->ibuf, size);
			memmove(slip->ibuf, &slip->ibuf[start+i+1],
				n - (i + 1));
			slip->more = n - (i + 1);
			return size;
		}
	}
	return 0;
}

int slip_proto_write(int fd, void *buf, int len, struct slip_proto *slip)
{
	int actual, n;

	actual = slip_esc(buf, slip->obuf, len);
	n = net_write(fd, slip->obuf, actual);
	if(n < 0)
		return n;
	else return len;
}
