#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <sched.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "net_user.h"
#include "slirp.h"
#include "slip_proto.h"
#include "helper.h"
#include "os.h"

void slirp_user_init(void *data, void *dev)
{
	struct slirp_data *pri = data;

	pri->dev = dev;
}

struct slirp_pre_exec_data {
	int stdin;
	int stdout;
};

static void slirp_pre_exec(void *arg)
{
	struct slirp_pre_exec_data *data = arg;

	if(data->stdin != -1) dup2(data->stdin, 0);
	if(data->stdout != -1) dup2(data->stdout, 1);
}

static int slirp_tramp(char **argv, int fd)
{
	struct slirp_pre_exec_data pe_data;
	int pid;

	pe_data.stdin = fd;
	pe_data.stdout = fd;
	pid = run_helper(slirp_pre_exec, &pe_data, argv, NULL);

	return(pid);
}

/* XXX This is just a trivial wrapper around os_pipe */
static int slirp_datachan(int *mfd, int *sfd)
{
	int fds[2], err;

	err = os_pipe(fds, 1, 1);
	if(err < 0){
		printk("slirp_datachan: Failed to open pipe, err = %d\n", -err);
		return(err);
	}

	*mfd = fds[0];
	*sfd = fds[1];
	return(0);
}

static int slirp_open(void *data)
{
	struct slirp_data *pri = data;
	int sfd, mfd, pid, err;

	err = slirp_datachan(&mfd, &sfd);
	if(err)
		return(err);

	pid = slirp_tramp(pri->argw.argv, sfd);

	if(pid < 0){
		printk("slirp_tramp failed - errno = %d\n", -pid);
		os_close_file(sfd);	
		os_close_file(mfd);	
		return(pid);
	}

	pri->slave = sfd;
	pri->pos = 0;
	pri->esc = 0;

	pri->pid = pid;

	return(mfd);
}

static void slirp_close(int fd, void *data)
{
	struct slirp_data *pri = data;
	int status,err;

	os_close_file(fd);
	os_close_file(pri->slave);

	pri->slave = -1;

	if(pri->pid<1) {
		printk("slirp_close: no child process to shut down\n");
		return;
	}

#if 0
	if(kill(pri->pid, SIGHUP)<0) {
		printk("slirp_close: sending hangup to %d failed (%d)\n",
			pri->pid, errno);
	}
#endif

	CATCH_EINTR(err = waitpid(pri->pid, &status, WNOHANG));
	if(err < 0) {
		printk("slirp_close: waitpid returned %d\n", errno);
		return;
	}

	if(err == 0) {
		printk("slirp_close: process %d has not exited\n");
		return;
	}

	pri->pid = -1;
}

int slirp_user_read(int fd, void *buf, int len, struct slirp_data *pri)
{
	int i, n, size, start;

	if(pri->more>0) {
		i = 0;
		while(i < pri->more) {
			size = slip_unesc(pri->ibuf[i++],
					pri->ibuf,&pri->pos,&pri->esc);
			if(size){
				memcpy(buf, pri->ibuf, size);
				memmove(pri->ibuf, &pri->ibuf[i], pri->more-i);
				pri->more=pri->more-i; 
				return(size);
			}
		}
		pri->more=0;
	}

	n = net_read(fd, &pri->ibuf[pri->pos], sizeof(pri->ibuf) - pri->pos);
	if(n <= 0) return(n);

	start = pri->pos;
	for(i = 0; i < n; i++){
		size = slip_unesc(pri->ibuf[start + i],
				pri->ibuf,&pri->pos,&pri->esc);
		if(size){
			memcpy(buf, pri->ibuf, size);
			memmove(pri->ibuf, &pri->ibuf[start+i+1], n-(i+1));
			pri->more=n-(i+1); 
			return(size);
		}
	}
	return(0);
}

int slirp_user_write(int fd, void *buf, int len, struct slirp_data *pri)
{
	int actual, n;

	actual = slip_esc(buf, pri->obuf, len);
	n = net_write(fd, pri->obuf, actual);
	if(n < 0) return(n);
	else return(len);
}

static int slirp_set_mtu(int mtu, void *data)
{
	return(mtu);
}

struct net_user_info slirp_user_info = {
	.init		= slirp_user_init,
	.open		= slirp_open,
	.close	 	= slirp_close,
	.remove	 	= NULL,
	.set_mtu	= slirp_set_mtu,
	.add_address	= NULL,
	.delete_address = NULL,
	.max_packet	= BUF_SIZE
};

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
