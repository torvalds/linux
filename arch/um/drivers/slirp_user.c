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
#include "slip_common.h"
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

static int slirp_open(void *data)
{
	struct slirp_data *pri = data;
	int fds[2], pid, err;

	err = os_pipe(fds, 1, 1);
	if(err)
		return(err);

	err = slirp_tramp(pri->argw.argv, fds[1]);
	if(err < 0){
		printk("slirp_tramp failed - errno = %d\n", -err);
		goto out;
	}
	pid = err;

	pri->slave = fds[1];
	pri->slip.pos = 0;
	pri->slip.esc = 0;
	pri->pid = err;

	return(fds[0]);
out:
	os_close_file(fds[0]);
	os_close_file(fds[1]);
	return err;
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
	return slip_proto_read(fd, buf, len, &pri->slip);
}

int slirp_user_write(int fd, void *buf, int len, struct slirp_data *pri)
{
	return slip_proto_write(fd, buf, len, &pri->slip);
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
